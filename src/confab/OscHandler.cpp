#include "OscHandler.hpp"

#include "Asset.hpp"
#include "AssetDatabase.hpp"
#include "CacheManager.hpp"
#include "Constants.hpp"
#include "HttpClient.hpp"
#include "schemas/FlatAsset_generated.h"
#include "schemas/FlatAssetData_generated.h"
#include "schemas/FlatList_generated.h"

#include "glog/logging.h"
#include "ip/UdpSocket.h"
#include "osc/OscOutboundPacketStream.h"
#include "osc/OscPacketListener.h"
#include "osc/OscReceivedElements.h"

#include <cstring>
#include <future>

namespace Confab {

/*! Handler class for processing incoming OSC messages.
 */
class OscHandler::OscListener : public osc::OscPacketListener {
public:
    /*! Constructor, needs a reference back to the containing OscHandler object.
     *
     * \param handler A non-owning pointer to the containing OscHandler.
     */
    OscListener(OscHandler* handler) :
        osc::OscPacketListener(),
        m_handler(handler) {
    }

    /*! Message handling function, called on each incoming OSC message.
     *
     * \param message Contains the message data.
     * \param endpoint Describes the sender.
     */
    void ProcessMessage(const osc::ReceivedMessage& message, const IpEndpointName& endpoint) override {
        try {
            if (std::strcmp("/assetFind", message.AddressPattern()) == 0) {
                osc::ReceivedMessage::const_iterator arguments = message.ArgumentsBegin();
                std::string assetIdString((arguments++)->AsString());
                if (arguments != message.ArgumentsEnd()) {
                    throw osc::ExcessArgumentException();
                }

                LOG(INFO) << "processing [/assetFind " << assetIdString << "]";

                uint64_t assetKey = Asset::stringToKey(assetIdString);
                if (assetKey == 0) {
                    LOG(ERROR) << "/assetFind got invalid key value: " << assetIdString;
                } else {
                    std::async(std::launch::async, [this, assetKey] {
                        m_handler->findAsset(assetKey);
                    });
                }
            } else if (std::strcmp("/assetFindName", message.AddressPattern()) == 0) {
                osc::ReceivedMessage::const_iterator arguments = message.ArgumentsBegin();
                std::string name((arguments++)->AsString());
                if (arguments != message.ArgumentsEnd()) {
                    throw osc::ExcessArgumentException();
                }

                LOG(INFO) << "processing [/assetFindName " << name << "]";
                std::async(std::launch::async, [this, name] {
                    m_handler->findNamedAsset(name);
                });
            } else if (std::strcmp("/assetLoad", message.AddressPattern()) == 0) {
                osc::ReceivedMessage::const_iterator arguments = message.ArgumentsBegin();
                std::string keyString((arguments++)->AsString());
                if (arguments != message.ArgumentsEnd()) {
                    throw osc::ExcessArgumentException();
                }

                LOG(INFO) << "processing [/assetLoad " << keyString << "]";

                uint64_t key = Asset::stringToKey(keyString);
                if (key == 0) {
                    LOG(ERROR) << "/assetLoad got invalid key value: " << keyString;
                } else {
                    std::async(std::launch::async, [this, key] {
                        m_handler->loadAsset(key);
                    });
                };
            } else if (std::strcmp("/assetAddFile", message.AddressPattern()) == 0) {
                osc::ReceivedMessage::const_iterator arguments = message.ArgumentsBegin();
                int serialNumber = (arguments++)->AsInt32();
                std::string typeString((arguments++)->AsString());
                std::string name((arguments++)->AsString());
                std::string authorString((arguments++)->AsString());
                uint64_t author = 0;
                if (authorString.size() > 0) {
                    author = Asset::stringToKey(authorString);
                }
                std::string deprecatesString((arguments++)->AsString());
                uint64_t deprecates = 0;
                if (deprecatesString.size() > 0) {
                    uint64_t deprecats = Asset::stringToKey(deprecatesString);
                }
                std::string listIds((arguments++)->AsString());
                std::string filePath((arguments++)->AsString());
                if (arguments != message.ArgumentsEnd()) {
                    throw osc::ExcessArgumentException();
                }

                LOG(INFO) << "processing [/assetAddFile " << typeString << ", " << serialNumber << ", " << name << ", "
                    << authorString << ", " << deprecatesString << ", " << filePath << "]";

                Asset::Type type = Asset::typeStringToEnum(typeString);
                if (type == Asset::kInvalid) {
                    LOG(ERROR) << "/assetAddFile got bad type string: " << typeString;
                } else {
                    std::async(std::launch::async, [this, type, serialNumber, name, author, deprecates, listIds,
                            filePath] {
                        m_handler->addAssetFile(type, serialNumber, name, author, deprecates, listIds, filePath);
                    });
                }
            } else if (std::strcmp("/assetAddString", message.AddressPattern()) == 0) {
                osc::ReceivedMessage::const_iterator arguments = message.ArgumentsBegin();
                int serialNumber = (arguments++)->AsInt32();
                std::string typeString((arguments++)->AsString());
                std::string name((arguments++)->AsString());
                std::string authorString((arguments++)->AsString());
                uint64_t author = 0;
                if (authorString.size() > 0) {
                    author = Asset::stringToKey(authorString);
                }
                std::string deprecatesString((arguments++)->AsString());
                uint64_t deprecates = 0;
                if (deprecatesString.size() > 0) {
                    deprecates = Asset::stringToKey(deprecatesString);
                }
                std::string listIds((arguments++)->AsString());
                std::string assetString((arguments++)->AsString());
                if (arguments != message.ArgumentsEnd()) {
                    throw osc::ExcessArgumentException();
                }

                LOG(INFO) << "processing [/assetAddString " << typeString << ", " << serialNumber << ", " << name
                    << ", " << authorString << ", " << deprecatesString << ", " << assetString << "]";

                Asset::Type type = Asset::typeStringToEnum(typeString);
                if (type == Asset::kInvalid) {
                    LOG(ERROR) << "/assetAddString got bad type string: " << typeString;
                } else {
                    std::async(std::launch::async, [this, type, serialNumber, name, author, deprecates, listIds,
                            assetString] {
                        m_handler->addAssetString(type, serialNumber, name, author, deprecates, listIds, assetString);
                    });
                }
            } else if (std::strcmp("/listAdd", message.AddressPattern()) == 0) {
                osc::ReceivedMessage::const_iterator arguments = message.ArgumentsBegin();
                std::string name((arguments++)->AsString());
                if (arguments != message.ArgumentsEnd()) {
                    throw osc::ExcessArgumentException();
                }

                LOG(INFO) << "processing [/listAdd, " << name << "]";

                std::async(std::launch::async, [this, name] {
                    m_handler->addList(name);
                });
            } else if (std::strcmp("/listFind", message.AddressPattern()) == 0) {
                osc::ReceivedMessage::const_iterator arguments = message.ArgumentsBegin();
                std::string name((arguments++)->AsString());
                if (arguments != message.ArgumentsEnd()) {
                    throw osc::ExcessArgumentException();
                }

                LOG(INFO) << "processing [/listFind, " << name << "]";

                std::async(std::launch::async, [this, name] {
                    m_handler->findList(name);
                });
            } else if (std::strcmp("/listNext", message.AddressPattern()) == 0) {
                osc::ReceivedMessage::const_iterator arguments = message.ArgumentsBegin();
                std::string keyString((arguments++)->AsString());
                uint64_t key = Asset::stringToKey(keyString);
                std::string tokenString((arguments++)->AsString());
                uint64_t token = Asset::stringToKey(tokenString);

                LOG(INFO) << "processing [/listNext, " << keyString << ", " << tokenString << "]";

                std::async(std::launch::async, [this, key, token] {
                    m_handler->nextList(key, token);
                });
            } else {
                LOG(ERROR) << "OSC unknown message: " << message.AddressPattern();
            }
        } catch (osc::Exception& exception) {
            LOG(ERROR) << "OSC error parsing message: " << message.AddressPattern() << ": " << exception.what();
        }
    }

private:
    OscHandler* m_handler;
};

OscHandler::OscHandler(int listenPort, int sendPort, std::shared_ptr<AssetDatabase> assetDatabase,
    std::shared_ptr<HttpClient> httpClient, std::shared_ptr<CacheManager> cacheManager) :
    m_listenPort(listenPort),
    m_sendPort(sendPort),
    m_assetDatabase(assetDatabase),
    m_httpClient(httpClient),
    m_cacheManager(cacheManager) {
}

OscHandler::~OscHandler() {
}

void OscHandler::run() {
    m_transmitSocket.reset(new UdpTransmitSocket(IpEndpointName("127.0.0.1", m_sendPort)));

    m_listener.reset(new OscListener(this));
    m_listenSocket.reset(new UdpListeningReceiveSocket(IpEndpointName(IpEndpointName::ANY_ADDRESS, m_listenPort),
        m_listener.get()));
    std::async(std::launch::async, [this] {
        m_listenSocket->Run();
    });
}

void OscHandler::shutdown() {
    m_listenSocket->AsynchronousBreak();
}

void OscHandler::findAsset(uint64_t assetId) {
    // First check database cache.
    RecordPtr databaseAsset = m_assetDatabase->findAsset(assetId);
    if (!databaseAsset->empty()) {
        LOG(INFO) << "database cache hit for asset " << Asset::keyToString(assetId) << " sending to SC.";
        sendAsset(Asset::keyToString(assetId), databaseAsset);
        return;
    }

    // Failing database cache, request from upstream server.
    m_httpClient->getAsset(assetId, [this, assetId](uint64_t loadedKey, RecordPtr record) {
        if (record->empty()) {
            char buffer[kDataChunkSize];
            osc::OutboundPacketStream p(buffer, kDataChunkSize);
            LOG(ERROR) << "failed to retrieve Asset " << Asset::keyToString(assetId) << ".";
            p << osc::BeginMessage("/assetError") << Asset::keyToString(assetId).c_str()
                << "Failed to find asset associated with key." << osc::EndMessage;
            m_transmitSocket->Send(p.Data(), p.Size());
        } else {
            // Store in database cache for future use.
            m_assetDatabase->storeAsset(loadedKey, record->data());
            // Send to client.
            LOG(INFO) << "downloaded asset " << Asset::keyToString(assetId) << " cached and sending to SC.";
            sendAsset(Asset::keyToString(assetId), record);
        }
    });
}

void OscHandler::findNamedAsset(std::string name) {
    // To ensure freshness of named Assets we don't refer to cache for them.
    m_httpClient->getNamedAsset(name, [this, &name](RecordPtr record) {
        if (record->empty()) {
            char buffer[kDataChunkSize];
            osc::OutboundPacketStream p(buffer, kDataChunkSize);
            LOG(ERROR) << "failed to retrieve named asset " << name << ".";
            p << osc::BeginMessage("/assetError") << name.c_str() << "Failed to find named asset." << osc::EndMessage;
            m_transmitSocket->Send(p.Data(), p.Size());
        } else {
            // TODO: clean up multiple parses here to extract key outside storeAsset, then again inside storeAsset,
            // then again in sendAsset(). Could refactor all methods to enforce conversion into FlatAsset and size
            // at time of *validation*.
            const Data::FlatAsset* flatAsset = Data::GetFlatAsset(record->data().data());
            m_assetDatabase->storeAsset(flatAsset->key(), record->data());
            LOG(INFO) << "downloaded named asset " << name << " with key "
                << Asset::keyToString(flatAsset->key()) << " cached and sending to SC.";
            sendAsset(name, record);
        }
    });
}

void OscHandler::loadAsset(uint64_t key) {
    uint64_t downloadKey = 0;
    fs::path assetPath = m_cacheManager->checkCache(key);

    if (assetPath.empty()) {
        LOG(INFO) << "file cache miss for asset " << Asset::keyToString(key) << ", downloading.";
        size_t size = 0;
        uint64_t chunks = 0;
        std::string fileExtension;
        // First check cache for this Asset.
        RecordPtr asset = m_assetDatabase->findAsset(key);
        if (asset->empty()) {
            LOG(INFO) << "cache miss for asset " << Asset::keyToString(key);
            m_httpClient->getAsset(key, [this, &downloadKey, &size, &chunks, &fileExtension](uint64_t loadedKey,
                RecordPtr record) {
                if (record->empty()) {
                    LOG(ERROR) << "asset not found " << Asset::keyToString(loadedKey);
                } else {
                    m_assetDatabase->storeAsset(loadedKey, record->data());
                    const Data::FlatAsset* flatAsset = Data::GetFlatAsset(record->data().data());
                    downloadKey = loadedKey;
                    size = flatAsset->size();
                    chunks = flatAsset->chunks();
                    fileExtension = flatAsset->fileExtension()->str();
                }
            });
        } else {
            LOG(INFO) << "cache hit for asset " << Asset::keyToString(key);
            const Data::FlatAsset* flatAsset = Data::GetFlatAsset(asset->data().data());
            // TODO: code duplication not great here.
            downloadKey = key;
            size = flatAsset->size();
            chunks = flatAsset->chunks();
            fileExtension = flatAsset->fileExtension()->str();
        }

        // We should have extracted what we need from the asset to download now.
        if (downloadKey != 0) {
            LOG(INFO) << "starting download for asset " << Asset::keyToString(downloadKey) << " for requested asset "
                << Asset::keyToString(key) << ", " << size << " bytes, " << chunks << " chunks, " << fileExtension;
            assetPath = m_cacheManager->download(downloadKey, size, chunks, fileExtension);
        } else {
            LOG(ERROR) << "unable to find Asset " << Asset::keyToString(key);
        }
    }

    char buffer[kPageSize];
    osc::OutboundPacketStream p(buffer, kPageSize);
    p << osc::BeginMessage("/assetLoaded")
        << Asset::keyToString(key).c_str()
        << Asset::keyToString(downloadKey).c_str()
        << assetPath.c_str()
        << osc::EndMessage;
    m_transmitSocket->Send(p.Data(), p.Size());
}

void OscHandler::addAssetFile(Asset::Type type, int serialNumber, std::string name, uint64_t author,
    uint64_t deprecates, std::string listIds, std::string filePath) {
    uint64_t key = m_httpClient->postFileAsset(type, name, author, deprecates, listIds, filePath);
    char buffer[kPageSize];
    osc::OutboundPacketStream p(buffer, kPageSize);
    p << osc::BeginMessage("/assetAdded") << serialNumber << Asset::keyToString(key).c_str()
        << osc::EndMessage;
    m_transmitSocket->Send(p.Data(), p.Size());
}

void OscHandler::addAssetString(Asset::Type type, int serialNumber, std::string name, uint64_t author,
    uint64_t deprecates, std::string listIds, std::string assetString) {
    uint64_t key = m_httpClient->postInlineAsset(type, name, author, deprecates, listIds, assetString.size(),
        reinterpret_cast<const uint8_t*>(assetString.c_str()));

    // Regardless of success or failure of Asset add we return the key and serial number.
    char buffer[kPageSize];
    osc::OutboundPacketStream p(buffer, kPageSize);
    p << osc::BeginMessage("/assetAdded") << serialNumber << Asset::keyToString(key).c_str()
        << osc::EndMessage;
    m_transmitSocket->Send(p.Data(), p.Size());
}

void OscHandler::sendAsset(const std::string& requested, RecordPtr record) {
    char buffer[kPageSize];
    osc::OutboundPacketStream p(buffer, kDataChunkSize);
    const Data::FlatAsset* asset = Data::GetFlatAsset(record->data().data());
    p << osc::BeginMessage("/assetFound");
    p << requested.c_str();
    p << Asset::keyToString(asset->key()).c_str();
    p << Asset::enumToTypeString(static_cast<Asset::Type>(asset->type())).c_str();
    p << asset->name()->c_str();
    p << Asset::keyToString(asset->author()).c_str();
    p << Asset::keyToString(asset->deprecatedBy()).c_str();
    p << Asset::keyToString(asset->deprecates()).c_str();
    if (asset->inlineData()) {
        osc::Blob blob(asset->inlineData()->data(), asset->inlineData()->size());
        p << blob;
    } else {
        osc::Blob blob(nullptr, 0);
        p << blob;
    }
    p << osc::EndMessage;
    m_transmitSocket->Send(p.Data(), p.Size());
}

void OscHandler::addList(std::string name) {
    uint64_t key = m_httpClient->postList(name);
    char buffer[kPageSize];
    osc::OutboundPacketStream p(buffer, kPageSize);
    if (key != 0) {
        p << osc::BeginMessage("/listFound") << name.c_str() << Asset::keyToString(key).c_str() << osc::EndMessage;
    } else {
        p << osc::BeginMessage("/listError") << name.c_str() << "error adding new list." << osc::EndMessage;
    }
    m_transmitSocket->Send(p.Data(), p.Size());
}

void OscHandler::findList(std::string name) {
    // We never cache these named entries, or really anything to do with lists (outside of Assets), for freshness
    // concerns.
    m_httpClient->getNamedList(name, [this, &name](RecordPtr record) {
        char buffer[kPageSize];
        osc::OutboundPacketStream p(buffer, kPageSize);
        if (record->empty()) {
            p << osc::BeginMessage("/listError") << name.c_str() << "error finding list." << osc::EndMessage;
        } else {
            const Data::FlatList* flatList = Data::GetFlatList(record->data().data());
            p << osc::BeginMessage("/listFound") << name.c_str() << Asset::keyToString(flatList->key()).c_str()
                << osc::EndMessage;
        }
        m_transmitSocket->Send(p.Data(), p.Size());
    });
}

void OscHandler::nextList(uint64_t key, uint64_t token) {
    m_httpClient->getListItems(key, token, [this, &key](const std::string& tokens) {
        char buffer[kPageSize];
        osc::OutboundPacketStream p(buffer, kPageSize);
        p << osc::BeginMessage("/listItems") << Asset::keyToString(key).c_str() << tokens.c_str() << osc::EndMessage;
        m_transmitSocket->Send(p.Data(), p.Size());
    });
}

}  // namespace Confab

