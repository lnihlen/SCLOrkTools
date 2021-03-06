SCLOrkClock : TempoClock {
	const historySize = 60;
	const <syncPort = 4249;

	classvar syncStarted;
	classvar <clockMap;
	classvar syncNetAddr;
	classvar timeDiffs;
	classvar timeDiffSum;
	classvar roundTripTimes;
	classvar roundTripTimeSum;
	classvar sumIndex;
	classvar <timeDiff;
	classvar changeQueue;
	classvar requestLastSent;
	classvar clockSyncOSCFunc;
	classvar syncTask;
	classvar wire;

	var <>currentState;
	var stateQueue;
	var beatSyncTask;

	*startSync { |serverName = "sclork-s01.local"|
		if (syncStarted.isNil, {
			syncStarted = true;
			clockMap = Dictionary.new;
			syncNetAddr = NetAddr.new(serverName, SCLOrkClockServer.syncPort);
			timeDiffs = Array.newClear(historySize);
			timeDiffSum = 0.0;
			roundTripTimes = Array.newClear(historySize);
			roundTripTimeSum = 0.0;
			sumIndex = 0;
			timeDiff = 0.0;
			changeQueue = PriorityQueue.new;

			clockSyncOSCFunc = OSCFunc.new({ | msg, time, addr |
				var serverTime = Float.from64Bits(msg[1], msg[2]);
				var currentTime = Main.elapsedTime;
				var diff = currentTime - serverTime;
				var roundTripTime = currentTime - requestLastSent;
				var n;

				if (timeDiffs[sumIndex].notNil, {
					timeDiffSum = timeDiffSum - timeDiffs[sumIndex];
					roundTripTimeSum = roundTripTimeSum - roundTripTimes[sumIndex];
					n = historySize.asFloat;
				}, {
					n = (sumIndex + 1).asFloat;
				});

				timeDiffs[sumIndex] = diff;
				roundTripTimes[sumIndex] = roundTripTime;
				sumIndex = (sumIndex + 1) % historySize;
				timeDiffSum = timeDiffSum + diff;
				roundTripTimeSum = roundTripTimeSum + roundTripTime;
				timeDiff = (timeDiffSum / n) - (roundTripTimeSum / (2.0 * n));
			},
			path: '/clockSyncSet',
			recvPort: syncPort,
			).permanent_(true);

			// SkipJack waits for timeout before executing first time, so
			// avoid situation where clocks created before first time sync
			// have times way off and skew to adjust.
			requestLastSent = Main.elapsedTime;
			syncNetAddr.sendMsg('/clockSyncGet', syncPort);

			syncTask = SkipJack.new({
				requestLastSent = Main.elapsedTime;
				syncNetAddr.sendMsg('/clockSyncGet', syncPort);
			},
			dt: 5.0,
			stopTest: { false },
			name: "SCLOrkClock Sync"
			);

			wire = SCLOrkWire.new(4248);
			wire.onConnected = { | wire, status |
				switch (status,
					\connected, {
						"*** connected to clock server.".postln;
						// Request curent list of all clocks.
						wire.sendMsg('/clockGetAll');
					},
					\failureTimeout, {
						"*** clock server connection failed.".postln;
						clockMap.clear;  // -- cleanup??
					},
					\disconnected, {
						clockMap.clear;  // -- cleanup??
					}
				);
			};
			wire.onMessageReceived = { | wire, msg |
				switch (msg[0],
					'/clockUpdate', {
						var state = SCLOrkClockState.newFromMessage(msg);
						var clock = clockMap.at(state.cohortName);
						if (clock.isNil, {
							var beats = state.secs2beats(Main.elapsedTime, timeDiff);
							var secs = state.beats2secs(beats, timeDiff);
							clock = super.new.init(state.tempo, beats, secs).prInit(state);
							clockMap.put(state.cohortName, clock);
							"/clockUpdate got new clock with state %".format(state.toString()).postln;
						}, {
							clock.prUpdate(state);
							"/clockUpdate updated clock with state %".format(state.toString()).postln;
						});
					},
				);
			};
			wire.knock(serverName, SCLOrkClockServer.knockPort);
		});
	}

	*serverToLocalTime { | serverTime |
		^(serverTime + timeDiff);
	}

	*localToServerTime { | localTime |
		^(localTime - timeDiff);
	}

	*new { |name = \default, tempo = 1.0, beatsPerBar = 4.0|
		var clock = nil;

		if (syncStarted.isNil or: { syncStarted.not } or: { timeDiffs.size < 5 }, {
			"*** call SCLOrkClock.startSync first.".postln;
		}, {
			clock = clockMap.at(name);
			if (clock.isNil, {
				var msg;
				var state = SCLOrkClockState.new(
					cohortName: name,
					applyAtTime: SCLOrkClock.localToServerTime(Main.elapsedTime),
					tempo: tempo,
					beatsPerBar: beatsPerBar);

				clock = super.new.init(tempo).prInit(state);
				clockMap.put(name, clock);

				// Inform server of clock creation.
				msg = state.toMessage;
				msg[0] = '/clockCreate';
				wire.sendMsg(*msg);
				"sent /clockCreate for %".format(name).postln;
			});
		});
		^clock;
	}

	// Moved from instance method .stop because will stop all clocks in the cohort.
	*stopClock { | name |
		wire.sendMsg('/clockStop', name);
	}

	prInit { |state|
		currentState = state;
		stateQueue = PriorityQueue.new;
		beatSyncTask = SkipJack.new({
			if (this.isRunning, {
				this.beats_(currentState.secs2beats(Main.elapsedTime, timeDiff));
			});
		},
		0.2);
	}

	prUpdate { | newState |
		// We ignore state changes for states calling for an earlier beat
		// than the current state's starting beat, because we can't
		// reliably compute times for states starting before our current
		// state.
		if (newState.applyAtBeat >= currentState.applyAtBeat, {
			// newState could be for a beat count that is earlier than our
			// current beat count. In that case we clobber the current state
			// with this one, which may require recomputation of scheduling, etc.
			// If newState is for a later beat count, it goes into the stateQueue,
			// and we schedule a task for later to promote it to the current state.
			if (newState.applyAtBeat <= this.beats, {
				"clobbering current state with new state for cohort %".format(newState.cohortName).postln;
				currentState = newState;
			}, {
				stateQueue.put(newState.applyAtBeat, newState);
			});

			// If we have a new state that may impact timing of state change schedules,
			// so we reschedule. And if we added a new state it may be the top state
			// change in the queue, so also schedule that.
			this.prScheduleStateChange;
		}, {
			"*** clock % dropping new state at beat %, before current state beat %.".format(
				currentState.cohortName, newState.applyAtBeat, currentState.applyAtBeat
			).postln;
		});
	}

	prStopLocal {

	}

	prScheduleStateChange {
		var nextBeat = stateQueue.topPriority;
		if (nextBeat.notNil, {
			var nextTime = this.beats2secs(nextBeat);
			SystemClock.schedAbs(nextTime, { this.prAdvanceState });
		});
	}


	prAdvanceState {
		var sec = Main.elapsedTime;
		var topBeat;
		while ({
			topBeat = stateQueue.topPriority;
			topBeat.notNil and: { topBeat <= this.beats }}, {
			currentState = stateQueue.pop;
		});

		if (topBeat.notNil, {
			var next = max(this.beats2secs(topBeat) - sec, 0.05);
			^next;
		}, {
			^nil;
		});
	}

	prSendChange { | state |
		var stateMsg = state.toMessage;
		stateMsg[0] = '/clockChange';
		wire.sendMsg(*stateMsg);
	}

	stop {
	}

	name {
		^currentState.cohortName;
	}

	tempo {
		^currentState.tempo;
	}

	tempo_ { | newTempo |
		var floatTempo = newTempo.asFloat;
		if (currentState.tempo != floatTempo, {
			var nextBeat = this.beats.roundUp.asFloat;
			this.setTempoAtBeat(floatTempo, nextBeat);
		});
	}

	secs2beats { |secs|
		^currentState.secs2beats(secs, timeDiff);
	}

	beats2secs { |beats|
		^currentState.beats2secs(beats, timeDiff);
	}

	setTempoAtBeat { | newTempo, beats |
		var state = currentState.setTempoAtBeat(newTempo, beats);
		this.prSendChange(state);
		super.setTempoAtBeat(newTempo, beats);
	}

	sendDiagnostic { |netAddr|
		var localTime = Main.elapsedTime;
		var serverTime = SCLOrkClock.localToServerTime(localTime);
		var localBeats = this.secs2beats(localTime);
		var bar = this.bar;
		var beatInBar = this.beatInBar;
		netAddr.sendMsg('/sclorkClockDiag',
			currentState.cohortName,
			localBeats.high32Bits, localBeats.low32Bits,
			localTime.high32Bits, localTime.low32Bits,
			serverTime.high32Bits, serverTime.low32Bits,
			timeDiff.high32Bits, timeDiff.low32Bits,
			bar.high32Bits, bar.low32Bits,
			beatInBar.high32Bits, beatInBar.low32Bits,
			currentState.applyAtTime.high32Bits, currentState.applyAtTime.low32Bits,
			currentState.applyAtBeat.high32Bits, currentState.applyAtBeat.low32Bits
		);
	}
}
