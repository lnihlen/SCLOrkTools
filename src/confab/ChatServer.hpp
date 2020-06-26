#ifndef SRC_CONFAB_CHAT_SERVER_HPP_
#define SRC_CONFAB_CHAT_SERVER_HPP_

#include "lo/lo.h"

#include <string>
#include <unordered_map>

namespace Confab {

/*! Implementation of the sclang-based SCLOrkServer using TCP and liblo instead.
 */
class ChatServer {
public:
    ChatServer();
    ~ChatServer();

    bool create(const std::string& bindPort);

    bool run();

    void stop();
    void destroy();

private:
    static void loError(int number, const char* message, const char* path);
    static int loHandle(const char* path, const char* types, lo_arg** argv, int argc, lo_message message,
                        void* userData);

    void handleMessage(const char* path, int argc, lo_arg** argv, const char* types, lo_address address);

    // Packs an ipv4 tuple + port from the lo_address into a 48-bit unsigned integer. Probably some endian assumptions
    // in here, but as long as this value doesn't leave this computer it should be fine. Returns 0 on error.
    uint64_t makeAddressToken(lo_address address);

    lo_server_thread m_tcpThread;
    lo_server m_tcpServer;

    int m_userSerial;

    // Map of userID to nickname strings.
    std::unordered_map<int, std::string> m_nameMap;

    // Map of address/port tokens to userID.
    std::unordered_map<uint64_t, int> m_addressMap;
};

} // namespace Confab

#endif // SRC_CONFAB_CHAT_SERVER_HPP_
