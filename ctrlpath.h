#ifndef CTRLPATH_H_
#define CTRLPATH_H_

#include "common.h"
#include "dpstate.h"

#include <map>
#include <queue>
#include <sstream>
//#include <algorithm>

// ccp-kernel에 구현된 netlink는 다음 코드를 보고 작성한 것 같음
// https://gist.github.com/arunk-s/c897bb9d75a6c98733d6

// UNIX Domain Socekt은 여러 자료가 있으나
// http://www.techdeviancy.com/uds.html 와

namespace quic {

    // TODO
    // string stream을 사용해서 Listen하고
    // string stream을 사용해서 Send하는 방법을 생각해보자.

    // UNIX Domain Socket을 사용하는 CCP Agent는
    // ListenUnixgram이라는 golang 함수를 사용해
    // Listen Socket을 만들어 연결을 대기하고 있으며,
    // 그 역할을 하는 unixsocket/socket.go에 SetupListen이 본체임.
    // SetupListen을 실제 실행하는 본체는 ipc/ipc.go에 있는
    // SetupCcpListen인데, 각 과정에서의 실행 순서를 따라갈 때 봐야 함.

    // netlink의 경우엔 Listen, Send, Finish 각각이 단순한데,
    // 소켓 설립 시 고정해둔 접속 파라미터를 사용하기 때문임
    // (여러 응용이 공유해서 쓰므로, 송/수신자는 메시지를 일단 모두 받고,
    // 메시지의 헤더 등을 확인해 받아야 할 것만 받도록 설계되어 있음)

    // 반면 UNIX Domain Socket는 Listen, Send, Finish가 일반
    // 네트워크 소켓 응용과 비슷한데, golang에서 ListenUnixgram과
    // DialUnix라는 함수를 통해 소켓 설립을 지원함.
    // UNIX Domain Socket은 소켓 설립을 위한 접속 파라미터로
    // 파일 경로를 주어야 함 (Kerrisk의 TLPI에 따르면, 이식성을 위해
    // 92바이트 아래로 하라고 한다.).
    // 문제는 이 파일 경로를 계산하는 부분을 netlink에선 쓰지 않는데
    // ipcBackend에 구현하였다는 것임. 이 때문에 CCP Agent의 golang
    // 소스를 번갈아가면서 읽어야 하는데, 요약하면 다음과 같다.

    // (1) 파일 경로 계산 함수들은 loc, id 두 개의 파라미터를 사용함
    // (2) SetupCcpListen에 따르면, "ccp-in", 0이 주어짐
    // (3) SetupCCpSend에 따르면 "ccp-out", (datapath에서 받은)
    //     sockid가 주어짐
    // (4) id가 0일 때엔 "/tmp/ccp-$loc"가 경로임
    // (5) id가 0이 아닐 때엔 "/tmp/ccp-$id/$loc"가 경로임
    // (6) AddressForListen 함수는 ccp-$id 디렉토리를 생성하는 역할도 함

    // 결국, 정리하면 다음과 같다.
    // [공동 채널]
    // (1) CCP Agent는 "/tmp/ccp-ccp-in"이라는 파일을 생성, 서버로 동작
    // (2) Datapath는 클라이언트로서 "/tmp/ccp-ccp-in"에 접속
    // [플로우 별 채널]
    // (1) Datapath는 "/tmp/ccp-$id/ccp-out"이라는 파일을 생성, 서버로 동작
    // (2) CCP Agent는 클라이언트로서 "/tmp/ccp-$id/ccp-out"에 접속

    class dpstate;

    typedef std::map<sockid, dpstate*> connMap; // 포인터만 저장하므로 dpstate는 반드시 다른 곳에서 저장해야 한다.
    extern connMap ccp_active_connections;
    extern std::stringstream cp_vlog;
    extern sockid to_agent_SocketId;

    sockid connect_agent(dpstate& state);

    sockid listen_ctrlpath(dpstate& state);

    void close_ctrlpath(dpstate& state);

    void recv_from_ctrlpath(dpstate& state);

    void recv_from_agent(char* buf);

    int send_to_agent(sockid socketId, char *msg, int msg_size);

        // send create msg
    int send_createmsg(sockid socketId, uint32_t startSeq, const char *alg);

    // send datapath measurements
    // ackNo, rtt, rin, rout
    int send_measuremsg(sockid socketId, ccp_measurement mnt);

    int send_dropnotif(sockid socketId, enum drop_type dtype);

    void ctrlPathController();

}

#endif // CTRLPATH_H_
