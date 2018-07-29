#ifndef CCP_SERIALIZE_H_
#define CCP_SERIALIZE_H_

// http://webnautes.tistory.com/1158


#include <cstddef> // for NULL
#include <cstdint> // for uint32_t etc.
#include <vector>


namespace quic {

// 파라미터 (구현시 가정)
#define BIGGEST_MSG_SIZE 256
#define MAX_STRING_SIZE  250

// CCP의 메시지는 Header와 메시지 페이로드 (Msg라고 불림)로 구성되어 있음

// 메시지 타입 (Msg Type 필드)

#define CREATE  0
#define MEASURE 1
#define DROP    2
#define PATTERN  3

// 메시지 헤더 정의
/* (type, len, socket_id) header
 * -----------------------------------
 * | Msg Type | Len (B)  | Uint32    |
 * | (1 B)    | (1 B)    | (32 bits) |
 * -----------------------------------
 * total: 6 Bytes
 */
struct __attribute__((packed, aligned(2))) CcpMsgHeader {
    uint8_t Type;
    uint8_t Len;
    uint32_t SocketId;
};

// 타입 별 메시지 페이로드
// 기본적으로 변경하지 않았다.

/*
 * CREATE:  1 개의 u32, 0 개의 u64, (최대 250바이트) str
 */
struct __attribute__((packed, aligned(4))) CreateMsg {
    uint32_t startSeq;
    char congAlg[MAX_STRING_SIZE];
};

/*
 * MEASURE: 3 개의 u32, 2 개의 u64, no str
 */
struct __attribute__((packed, aligned(4))) MeasureMsg {
    uint32_t ackNo;
    uint32_t rtt;
    uint32_t loss;
    uint64_t rin;
    uint64_t rout;
};

enum drop_type {
    NO_DROP,
    DROP_TIMEOUT,
    DROP_DUPACK,
    DROP_ECN
};

/*
 * DROP:    0 u32, 0 u64, str
 * type 필드는 drop_type으로 의미를 해석할 수 있음
 */
struct __attribute__((packed, aligned(4))) DropMsg {
    char type[MAX_STRING_SIZE];
};

/*
 * PATTERN:  1 개의 u32, 0 u64, str
 */
struct __attribute__((packed, aligned(4))) PatternMsg {
    uint32_t numStates;
    char pattern[MAX_STRING_SIZE];
};

#define SETRATEABS 0
#define SETCWNDABS 1
#define SETRATEREL 2
#define WAITABS    3
#define WAITREL    4
#define REPORT     5

/* CCP Agent는 한 패턴 메시지 안에 여러 개의 상태를 묶어서 보낸다.
 * 저자의 주석에 따르면 CCP Agent에서 오는 모는 메시지는 PatternMsg라고...
 * char 배열로 그 내용을 까보기 위해서는 PatternEvent 구조체를 이용해야 한다.
 * 패턴 이벤트의 type은 위에 정의되어 있다.
*/
struct __attribute__((packed, aligned(2))) PatternEvent {
    uint8_t type;
    uint8_t size;
    uint32_t val;
};

/* 버퍼에서 읽어온 메시지에서 헤더 부분만 hdr이 가리키는 주소에 저장한다.
 * 정의된 타입이면 저장한 헤더 크기를 리턴한다. 아니면 에러 -1을 리턴한다.
 * 저자의 코드를 그대로 가지고 왔으며 에러 처리가 안 된 점에 주의.
*/
int readHeader(struct CcpMsgHeader *hdr, char *buf);

/* readHeader의 역연산. hdr에 있는 값을 읽어서 buf에 쓰는 것이 목적이다.
 * 입력받은 버퍼의 크기 에러 체크를 하고 있다. (에러시 -2)
 * 저자의 코드를 그대로 가져왔다.
 * return: number of bytes written to buf
 */
int serializeHeader(char *buf, int bufsize, struct CcpMsgHeader *hdr);

/* MeasureMsg를 buf에 쓰는 것을 목표로 한다. 에러 체크를 한다. 저자 코드 그대로 가져왔음
 * 이 함수는 사용자가 부르기 위해 존재하는 것이 아니다.
 * return: number of bytes written to buf
 */
int serializeMeasureMsg(char *buf, int bufsize, struct MeasureMsg *msg);

/* CreateMsg는 시작 sequence 번호와 사용할 혼잡 제어 알고리즘을 페이로드에 담아야 한다.
 * write라고 붙어있는 함수는 sid를 인자로 받고 있는데, 이는 주고받는 CCP 메시지의 Header에
 * socket ID를 담아 전송하기 때문이다.
 * 아래 세 가지 함수는 저자가 만든 것에서 수정된 것이 없다.
 * return: number of bytes written to buf
 */
int writeCreateMsg (char *buf, int bufsize, uint32_t sid,
                    uint32_t startSeq, const char* str);
int writeMeasureMsg(char *buf, int bufsize, uint32_t sid,
                    uint32_t ackNo, uint32_t rtt, uint32_t loss, uint64_t rin, uint64_t rout);
int writeDropMsg   (char *buf, int bufsize, uint32_t sid,
                    const char* str);

/* CCP 메시지를 읽는 일반적인 함수이다.
 * hdr와 msg를 저장할 포인터와, 읽지 않은 메시지의 주소 buf를 주면 hdr, msg를 채운다.
 * 지역 변수인 buf는 내부적으로 값이 변하므로 const 가 붙어있지 않다.
 * return: size of msg
 */
int readMsg(struct CcpMsgHeader *hdr, struct PatternMsg *msg, char *buf);



typedef std::vector<PatternEvent> Events;


/* 헤더가 제거된 PatternMsg로부터 PatternEvent를 읽어온다.
 * 다만, PatternMsg를 직접 파싱하는 게 아니라, PatternMsg 구조체에서
 * PatternEvent의 숫자 numEvents를 읽어 인자로 넘겨주어야 하며,
 * PatternMsg 안에 담긴 PatternEvent를 접근하기 위해
 * pattern 포인터에 그 주소를 기록해야 한다.
 *
 * 노희준이 C++ STL에 기반하여 새로 작성하였다.
 *
 * if no val in event, set to 0
 *
 * seq: array of PatternEvents
 * return: 0 if ok, -1 otherwise
 */
int readPattern(Events& seq, char *pattern, int numEvents);


} // namespace quic

#endif /* CCP_SERIALIZE_H_ */