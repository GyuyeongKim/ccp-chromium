#ifndef DPSTATE_H_
#define DPSTATE_H_
#include "common.h"
#include "serialize.h"
#include "ctrlpath.h"

#include <chrono> // millisecond time
#include <sstream>

// Google QUIC에서는 시간을 관리하는 복잡한 함수를 지원하고 있는데,
// 이를 dpstate에 반영하면 유닛 테스트가 힘들어지는 문제가 있음
// 따라서 dpstate에는 dp_time이라는 typedef로 선언해서 처리하고,
// 시간 함수를 실제 함수에서 호출한 CCP_KERNEL과 달리
// 인자로 넘겨주는 형태로 하자. 다만 now 변수에 인자를 안 줄 경우
// private에 저장한 함수포인터를 활용하자.

namespace quic {

    // Written by the google team
    // history https://github.com/torvalds/linux/commits/master/net/ipv4/tcp_rate.c
    // located in include/net/tcp.h
    // A rate sample measures the number of (original/retransmitted) data
    // packets delivered "delivered" over an interval of time "interval_us".
    // The tcp_rate.c code fills in the rate sample, and congestion
    // control modules that define a cong_control function to run at the end
    // of ACK processing can optionally chose to consult this sample when
    // setting cwnd and pacing rate.
    // A sample is invalid if "delivered" or "interval_us" is negative.
    struct rate_sample {
        int32_t delivered; 		/* number of packets delivered over interval */
        long interval_us; 	/* time for tp->delivered to incr "delivered" */
        long snd_int_us; 	/* snd interval for delivered packets */				// added by ngsrinivas/linux-fork
        long rcv_int_us; 	/* rcv interval for delivered packets */				// added by ngsrinivas/linux-fork
        long rtt_us;		/* RTT of last (S)ACKed packet (or -1) */
        int losses;			/* Rnumber of packets marked lost upon ACK */
    };

    // located in include/net/tcp.h
    struct ack_sample {
        uint32_t pkts_acked;
        int32_t rtt_us;
    };

    typedef uint64_t dp_time;

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
            sockid socketId; // Listen Socket, ccp_index에 해당되는 듯.
            sockid agent; // agent는 보낼 message queue를 가지고 있다고 하자.
            dp_time (*now)();
            dp_time (*mus)();
            
            // CCP TCP STATE
            uint32_t snd_rate;   /* ca->rate */            
            Events seq; // struct PatternEvent *pattern;
            uint8_t currPatternEvent;
            // uint8_t numPatternEvents;는 seq.len()으로 계산 가능하니 무시
            dp_time next_event_time;
            dp_time now_time;
            dp_time mus_time;
            enum drop_type last_drop_state;
            uint32_t num_loss;

            ccp_measurement smoothed; // struct ccp_measurement mmt; 인듯? 실제로는 ackNo와 loss는 smoothed가 아님.

            uint8_t ca_state; // congestion algorithm state tcp_ca_state (어디다 쓰는 거지?)

            bool doSetCwndAbs(uint32_t cwnd);
            bool doSetRateAbs(uint32_t rate);
            bool doSetRateRel(uint32_t factor);
            bool doReport();
            bool doWaitAbs(uint32_t wait_us);
            bool doWaitRel(uint32_t rtt_factor);

            void log_seq();

            static int rate_sample_valid(const struct rate_sample *rs);
            static uint64_t ewma(uint64_t old, uint64_t __new);            

        public:
            //tcp_sock
            /* RFC793 variables by their proper names. this means you can
            * read the code and the spec side by side (and laugh ...)
            * See RFC793 and RFC1122. The RFC writes these in capitals. */
            uint32_t snd_una; /* First byte we want an ack for */ // ackNo 의미함
            /* Data for direct copy to user */
            uint32_t mss_cache; 	/* Cached effective mss, not including SACKS */
            /* Slow start and congestion control (see also Nagle, and Karn & Patridge) */
            uint32_t snd_ssthresh; /* Slow start size threshold */
            uint32_t snd_cwnd;	/* sending congestion window */

            dpstate();

            bool sync_with_agent(dp_time now);              // sendStateMachine
            bool sync_from_agent(Events& seq, dp_time now); // installPattern
 
            // tcp_congestion_ops
            // 참고: http://netlab.caltech.edu/projects/ns2tcplinux/ns2linux/tutorial/index.html
            // 참고: http://www.yonch.com/tech/linux-tcp-congestion-control-internals
            void congestion_control(const struct rate_sample *rs); // main 함수에 해당
            uint32_t get_ssthresh();    // ssthresh
            uint32_t undo_cwnd();       // undo_cwnd
            void pkts_acked(const struct ack_sample *sample); // pkts_acked
            void set_state(uint8_t new_state);  // set_state
            void set_pacing_rate(); // from tcp_ccp.cc ccp_set_pacing_rate

            // etc
            void set_agent(sockid agent);
            std::stringstream vlog;
            void print_log();

            void set_socketId(sockid socketId);
            sockid get_socketId();
            void set_clock_fn(dp_time (*now)());
            void set_mus_to_dp_time_fn(dp_time (*mus)());
            dp_time get_now();
            dp_time get_mus_to_dp_time_fn();
    };


} // namespace quic

#endif // DPSTATE_H_