#include "ctrlpath.h"
#include "serialize.h"


#include <unistd.h> // fnctl
#include <fcntl.h> // fnctl
#include <sys/un.h> // unix domain socket
#include <sys/socket.h> // socket
#include <cstring> // memcpy
#include <cstdio> // sprintf
#include <unistd.h> // close
#include <sys/stat.h> // mkdir
#include <sys/types.h> // DIR type
#include <dirent.h> // opendir

#define CTRLPATH_BUF_SIZE   4096

namespace quic {

connMap ccp_active_connections;
std::stringstream cp_vlog;
sockid to_agent_SocketId;

// 서버에 접속
sockid connect_agent(dpstate& state) {
    // TODO:
    int socketId;
    const char to_agent[] = "/tmp/ccp-ccp-in";
    struct sockaddr_un addr;

    socketId = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (socketId == -1) {
        cp_vlog << "socket creation error!" << strerror(errno) << std::endl;
        to_agent_SocketId = ((sockid) -1);
        return ((sockid) -1);
    }
    cp_vlog << "socket created: " << socketId << std::endl;

    std::memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, to_agent, sizeof(addr.sun_path) - 1);

    if(connect(socketId, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1) {
        cp_vlog << "connect error! " << strerror(errno) << std::endl;
        to_agent_SocketId = ((sockid) -1);
        return ((sockid) -1);
    }

    // TODO: 주의. CCP Agent가 하나 뿐이라고 생각하고 설계함
    to_agent_SocketId = socketId;

    // TODO: dpstate* state를 사용하지 않음. 사용해야 할 이유가 있을까?

    return ((sockid) socketId);
}

// 클라이언트를 기다림
sockid listen_ctrlpath(dpstate& state) {
    // TODO:

    // [플로우 별 채널]
    // (1) Datapath는 "/tmp/ccp-$id/ccp-out"이라는 파일을 생성, 서버로 동작
    // (2) CCP Agent는 클라이언트로서 "/tmp/ccp-$id/ccp-out"에 접속

    int socketId;
    struct sockaddr_un addr;

    socketId = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (socketId == -1) {
        cp_vlog << "socket creation error! " << strerror(errno) << std::endl;
        to_agent_SocketId = ((sockid) -1);
        return ((sockid) -1);
    }

    /* setting non-blocking mode for UDS */
    // https://kldp.org/node/99263
    int     flags;
    if ((flags = fcntl(socketId, F_GETFL, 0)) < 0 ||
        fcntl(socketId, F_SETFL, flags | O_NONBLOCK) < 0) {
                cp_vlog << "setting O_NONBLOCK " << strerror(errno) << std::endl;
        return ((sockid) -1);
    }    

    char from_agent[92] = {0,}; // 92 바이트는 Kerrisk 참고
    int len;
    //len = snprintf(from_agent, 92, "/tmp/ccp-ccp-out", socketId);
    len = snprintf(from_agent, 92, "/tmp/ccp-%d", socketId);

    // TODO: 디렉토리 생성
    int err;

    DIR* dir = opendir("from_agent");
    if(dir) { // directory exists
        closedir(dir);
    } else if (ENOENT == errno) {
        err = mkdir(from_agent, 0755);
        if(err == -1) {
            cp_vlog << "Warning in mkdir: " << strerror(errno) << std::endl;
        }
    } else {
        cp_vlog << "open directory in " << from_agent << " error! " << strerror(errno) << std::endl;
        return ((sockid) -1);
    }

    len = snprintf(from_agent + len, 92 - len, "/ccp-out");

    if (remove(from_agent) == -1 && errno != ENOENT) {
        cp_vlog << "remove " << from_agent << " error! " << strerror(errno) << std::endl;
        return ((sockid) -1);
    }

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, from_agent, sizeof(addr.sun_path) - 1 );

    if (bind(socketId, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1) {
        cp_vlog << "bind error " << strerror(errno) << std::endl;
        return ((sockid) -1);
    }

    // if (listen(socketId, 1) == -1) { // BACKLOG를 1로 설정했는데 적절할까?
    //     cp_vlog << "listen error " << strerror(errno) << std::endl;
    //     return ((sockid) -1);
    // }

    // TODO: 중요: dpstate 객체를 생성해서 대입하여야 함
    // 얇은 복사가 문제를 발생시킬 수 있음
    ccp_active_connections[socketId] = &state;

    state.set_socketId(socketId);
    std::cout << "State's socket ID: " << socketId << " " << state.get_socketId() << std::endl;

    return ((sockid) socketId);
}

void close_ctrlpath(dpstate& state) {
    // TODO:
    sockid socketId = state.get_socketId();
    if (close(socketId) == -1) {
        cp_vlog << "close error " << strerror(errno) << std::endl;
        return;
    }

    if (socketId == to_agent_SocketId) { // 
        cp_vlog << "cannot be occured..." << std::endl;
        to_agent_SocketId = ((sockid) -1);
        return;
    }

    ccp_active_connections.erase(socketId);
}

void close_agent() {
    // TODO:
    if (close(to_agent_SocketId) == -1) {
        cp_vlog << "close error " << strerror(errno) << std::endl;
        return;
    }
    to_agent_SocketId = ((sockid) -1);
}

void recv_from_ctrlpath(dpstate& state) {
    int numRead;
    char buf[CTRLPATH_BUF_SIZE];
    if((numRead = read(state.get_socketId(), buf, CTRLPATH_BUF_SIZE)) > 0) {
        std::cout << "Read from CCP Agent! -------------------------------------------" << std::endl;
        recv_from_agent(buf);
        // TODO: 현재 코드는 buf에 들어온 메시지가 정상인 것만 가정함
        // The assumptiuon is plausible: SOCK_DGRAM is used in the socket!
        // 예를 들어 1바이트가 잘못 추가되면 전체 메시지를 읽기만하고 날리게 됨
    }

    if(numRead == -1) {
        cp_vlog << "read error " << strerror(errno) << std::endl;
    }
}

void recv_from_agent(char* buf) {
    // nl_recv 함수의 대체
    // TODO: Should it contain read function...?
    int err;
    struct CcpMsgHeader hdr; // serialize.h
    struct PatternMsg msg; // serialize.h

    // 도착하는 메시지는 모두 PatternMsg라고 가정
    // readMsg도 그렇게 설계되어 있음

    err = readMsg(&hdr, &msg, buf);
    if (err < 0) {
        // serialize에 정의된 형식대로 패턴 메시지를 읽으려고 하였으나 실패
        std::cout << "failed to read" << std::endl;
        return;
    }

    connMap::iterator it = ccp_active_connections.find(hdr.SocketId);
    if(it == ccp_active_connections.end()) {
        // 들어본 적 없는 메시지라면 에러 없이 리턴
        // 이 이야기는 netlink의 모든 메시지를 듣고 그에 대해 이 함수가
        // 실행되고 있었음을 의미함
        std::cout << "unseen connection" << std::endl;
        return;
    }

    Events seq;
    err = readPattern(seq, msg.pattern, msg.numStates);
    if(err < 0) {
        // 정상적인 경우 0, 이상인 경우 -1
        std::cout << "unreadable pattern" << std::endl;
        return;
    }

    std:: cout << "read complete!" << std::endl;

    ((it)->second)->sync_from_agent(seq, ((it)->second)->get_now()); // sk는 소켓 ID
}

int send_to_agent(sockid socketId, char *msg, int msg_size) {
    if(msg_size <=0) {
        cp_vlog << "strange msg_size" << std::endl;
        return 0;
    }
    int numWrite;
    if((numWrite = write(socketId, msg, msg_size)) != msg_size) {
        cp_vlog << "write error " << strerror(errno) << std::endl;
        return -1;
    }

    return msg_size;
}

void ctrlPathController() {
    // TODO: 쓰레드로 구현할 경우에 사용하기 위한 함수.
    // 다만 현실적으로 QUIC의 이벤트 콜백으로 위 함수들을 사용하는 게
    // 현실적일 듯

    dpstate *testState;
    testState = new dpstate();
    sockid socketId = connect_agent(*testState);

    std::cout << "To Agent Socket ID: " << socketId << " " << to_agent_SocketId << std::endl;
    std::cout << cp_vlog.str() << std::endl;
    cp_vlog.clear();

    listen_ctrlpath(*testState);
    std::cout << cp_vlog.str() << std::endl;
    cp_vlog.clear();

    testState->ackNo = 0;
    char buf[CTRLPATH_BUF_SIZE];
    memset(buf, 0, CTRLPATH_BUF_SIZE);
    std::cout << "Get Socket ID: " << testState->get_socketId() << std::endl;
    int len = writeCreateMsg(buf, CTRLPATH_BUF_SIZE, testState->get_socketId(), (testState->ackNo)++, "cubic");
    if (len > 0)
        send_to_agent(to_agent_SocketId, buf, len);

    std::cout << cp_vlog.str() << std::endl;
    cp_vlog.clear();

    recv_from_ctrlpath(*testState);
    std::cout << cp_vlog.str() << std::endl;
    cp_vlog.clear();

    std::cout << "GO TO WHILE" << std::endl;

    while(1) {
        //memset(buf, 0, CTRLPATH_BUF_SIZE);
        // for(int j=0;j<10;j++) {
            // writeMeasureMsg(buf, CTRLPATH_BUF_SIZE, testState->get_socketId(),
            //             (testState->ackNo)++, 10, 0, 1, 1);
            // if (len > 0)
            //     send_to_agent(to_agent_SocketId, buf, len);
            // std::cout << cp_vlog.str() << std::endl;
            // cp_vlog.clear();
            for(int i=0;i<1000000;i++) {
                std::cout << "";
            }
        // }

        // std::cout << cp_vlog.str() << std::endl;
        // cp_vlog.clear();
        testState->sync_with_agent(testState->get_now());
        testState->print_log();
        recv_from_ctrlpath(*testState);
    }

    close_ctrlpath(*testState);    
    close_agent();
}

} // namespace quic