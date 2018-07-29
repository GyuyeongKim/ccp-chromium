#ifndef DPSTATE_H_
#define DPSTATE_H_
#include "common.h"
#include "serialize.h"
#include "ctrlpath.h"

#include <sstream>

// Google QUIC에서는 시간을 관리하는 복잡한 함수를 지원하고 있는데,
// 이를 dpstate에 반영하면 유닛 테스트가 힘들어지는 문제가 있음
// 따라서 dpstate에는 dp_time이라는 typedef로 선언해서 처리하고,
// 시간 함수를 실제 함수에서 호출한 CCP_KERNEL과 달리
// 인자로 넘겨주는 형태로 하자. 다만 now 변수에 인자를 안 줄 경우
// private에 저장한 함수포인터를 활용하자.

namespace quic {

    typedef uint64_t dp_time;

    typedef MeasureMsg ccp_measurement;

    // TCP의 상태인데, 이를 quic에 맞추어 바꾸어야 함
    // include/uapi/linux/tcp.h
    enum tcp_ca_state {
        TCP_CA_Open = 0,
    #define TCPF_CA_Open	    (1<<TCP_CA_Open)
        TCP_CA_Disorder = 1,
    #define TCPF_CA_Disorder    (1<<TCP_CA_Disorder)
        TCP_CA_CWR = 2,
    #define TCPF_CA_CWR	        (1<<TCP_CA_CWR)
        TCP_CA_Recovery = 3,
    #define TCPF_CA_Recovery    (1<<TCP_CA_Recovery)
        TCP_CA_Loss = 4
    #define TCPF_CA_Loss	    (1<<TCP_CA_Loss)
    };


    class dpstate {
        private:
            sockid socketId; // Listen Socket
            ctrlPath* agent; // agent는 보낼 message queue를 가지고 있다고 하자.
            dp_time (*now)();
            dp_time (*mus)();

            Events seq;
            uint8_t currPatternEvent;
            dp_time next_event_time;
            dp_time now_time;
            dp_time mus_time;

            uint8_t ca_state; // congestion algorithm state tcp_ca_state

            //tcp_sock
            /* RFC793 variables by their proper names. this means you can
            * read the code and the spec side by side (and laugh ...)
            * See RFC793 and RFC1122. The RFC writes these in capitals. */
            uint32_t snd_una; /* First byte we want an ack for */
            /* Data for direct copy to user */
            uint32_t mss_cache; 	/* Cached effective mss, not including SACKS */
            /* Slow start and congestion control (see also Nagle, and Karn & Patridge) */
            uint32_t snd_ssthresh; /* Slow start size threshold */
            uint32_t snd_cwnd;	/* sending congestion window */

            uint32_t snd_rate;   /* ca->rate */

            ccp_measurement sample;

            bool doSetCwndAbs(uint32_t cwnd);
            bool doSetRateAbs(uint32_t rate);
            bool doSetRateRel(uint32_t factor);
            bool doReport();
            bool doWaitAbs(uint32_t wait_us);
            bool doWaitRel(uint32_t rtt_factor);

            void log_seq();

        public:
            dpstate();
            std::stringstream vlog;

            void set_socketId(sockid socketId) {
                this->socketId = socketId;
            }
            sockid get_socketId() {
                return socketId;
            }

            dp_time get_now() {return now_time;};
            dp_time get_mus_to_dp_time_fn() {return mus_time; };
            void set_agent(ctrlPath* agent);
            void set_clock_fn(dp_time (*now)());
            void set_mus_to_dp_time_fn(dp_time (*mus)());
            bool sync_with_agent(dp_time now = 0);              // sendStateMachine
            bool sync_from_agent(Events& seq, dp_time now = 0); // installPattern

            // dpstate& dpstate::operator=(const dpstate& p) {
            //     socketId = p.socketId;
            //     agent = p.agent;
            //     now = p.now;
            //     mus = p.mus;

            //     seq.clear();
            //     for(Events::iterator it = (p.seq).begin(); it != (p.seq).end(); it++) {
            //         seq.push_back(*it);
            //     }
            
            //     currPatternEvent = p.currPatternEvent;
            //     next_event_time = p.next_event_time;
            //     now_time = p.now_time;
            //     mus_time = p.mus_time;
                
            //     ca_state = p.ca_state;
                
            //     snd_una = p.snd_una;
            //     mss_cache = p.mss_cache;
            //     snd_ssthresh = p.snd_ssthresh;
            //     snd_cwnd = p.snd_cwnd;

            //     snd_rate = p.snd_rate;
                
            //     memcpy(&sample, &(p.sample), sizeof(ccp_measurement)); 
            // }
    };


} // namespace quic

#endif // DPSTATE_H_