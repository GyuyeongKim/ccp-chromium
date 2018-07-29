#include "dpstate.h"

#include <string>
#include <algorithm> // for max
#include <cstring> // memcpy

namespace quic {

#define CCP_FRAC_DENOM 10
#define CCP_EWMA_RECENCY 6

#define MTU 1500
#define S_TO_US 1000000


dpstate::dpstate() { // tcp_ccp_init
    socketId = 0;
    mss_cache = 1; // TODO: mss_cache는 linux에서 제공

    next_event_time = get_now();
    currPatternEvent = 0;
    last_drop_state = NO_DROP;
    num_loss = 0;

    ccp_measurement init_mmt = {
        .ackNo = 0,
        .rtt = 0,
        .rin = 0, /* send bandwidth in bytes per second */
        .rout = 0, /* recv bandwidth in bytes per second */
        .loss = 0,
    };
    std::memcpy(&(smoothed), &init_mmt, sizeof(ccp_measurement));    
}

bool dpstate::doSetCwndAbs(uint32_t cwnd) {
    cwnd /= mss_cache;
    vlog << "cwnd " << snd_cwnd << " -> " << "( mss " << mss_cache << ")" << std::endl;
    snd_cwnd = cwnd;
    return false;
}

bool dpstate::doSetRateAbs(uint32_t rate) {
    vlog << "rate (Bytes/s) -> " << rate << std::endl;
    snd_rate = rate;
    set_pacing_rate();
    return true;
}

bool dpstate::doSetRateRel(uint32_t factor) {
    uint64_t newrate = snd_rate * factor;
    newrate = newrate / 100;
    vlog << "rate -> " << newrate << std::endl;
    snd_rate = newrate;
    set_pacing_rate();
    return true;
}

bool dpstate::doReport() {
    vlog << "report = (" << smoothed.ackNo << ", " << smoothed.rtt << ", " << smoothed.loss << ", " << smoothed.rin << ", " << smoothed.rout << ")" << std::endl;
    // TODO: nl_send_measurement;
    char buf[4096];
    int len = writeMeasureMsg(buf, 4096, socketId,
                        smoothed.ackNo++, 10, 0, 1, 1);
            if (len > 0)
                send_to_agent(to_agent_SocketId, buf, len);

    return true;
}

bool dpstate::doWaitAbs(uint32_t wait_us) {
    vlog << "waiting " << wait_us << " us" << std::endl;
    next_event_time = get_now() + wait_us * get_mus_to_dp_time_fn();
    return false;
}

bool dpstate::doWaitRel(uint32_t rtt_factor) {
    uint64_t rtt_us = smoothed.rtt;
    uint64_t wait_us = rtt_factor * rtt_us;
    wait_us = wait_us / 100;
    vlog << "waiting " << wait_us << " us (" << rtt_factor << "/100 rtts) (rtt = " << rtt_us << " us)" << std::endl;
    next_event_time = get_now() + wait_us * get_mus_to_dp_time_fn();
    return false;
}

void dpstate::log_seq() {
    vlog << "installed pattern:" << std::endl;
    int i = 0;
    for(Events::iterator it = seq.begin(); it != seq.end(); it++) {
        switch(it->type) {
            case SETRATEABS:
                vlog << "[ev " << i << "] set rate " << it->val << std::endl;
                break;
            case SETCWNDABS:
                vlog << "[ev " << i << "] set cwnd " << it->val << std::endl;
                break;
            case SETRATEREL:
                vlog << "[ev " << i << "] set rate factor " << it->val << "/100" << std::endl;
                break;
            case WAITREL:
                vlog << "[ev " << i << "] wait " << it->val << " us" << std::endl;
                break;
            case REPORT:
                vlog << "[ev " << i << "] send report" << std::endl;
                break;
        }
        i++;
    }
}

void dpstate::set_agent(sockid agent) {
    this->agent = agent;
}

void dpstate::set_clock_fn(dp_time (*now)()) {
    this->now = now;
}

void dpstate::set_mus_to_dp_time_fn(dp_time (*mus)()) {
    this->mus = mus;
}

bool dpstate::sync_with_agent(dp_time now) {              // sendStateMachine
    if(seq.size() == 0) {
        // 연결이 아직 안 맺어진 것이거나 끊긴 것이니 CCP Agent 접속함
        // TODO: nl_send_conn_create(ccp_index, snd_una);

        return true;
    }

    if(now > next_event_time) {
        currPatternEvent = (currPatternEvent + 1) % seq.size();
        vlog << "curr pattern event: " << (uint32_t) currPatternEvent << std::endl;
    } else {
        return false;
    }

    struct PatternEvent ev = seq[currPatternEvent];
    switch(ev.type) {
        case SETRATEABS:
            return doSetRateAbs(ev.val);
            break;
        case SETCWNDABS:
            return doSetCwndAbs(ev.val);
            break;
        case SETRATEREL:
            return doSetRateRel(ev.val);
            break;
        case WAITREL:
            return doWaitRel(ev.val);
            break;
        case WAITABS:
            return doWaitAbs(ev.val);
            break;
        case REPORT:
            return doReport();
            break;
    }


    vlog << "[ev ] unrecognizable report" << std::endl;
    return true;
}

bool dpstate::sync_from_agent(Events& seq, dp_time now) { // installPattern
    this->seq = seq;
    log_seq();    
    currPatternEvent = seq.size() - 1;
    next_event_time = get_now();
    std::cout << "sync_from_agent" << std::endl << vlog.str() << std::endl;
    std::cout << "new seq size: " << (unsigned int)currPatternEvent << std::endl;
    std::cout << "now: " << now << std::endl;
    std::cout << "next_event_time: " << next_event_time << std::endl;
    print_log();
    return sync_with_agent(now);
}

/* 역할: ccp congestion control이 저장한 상태에서 소켓의 pacing rate를
 * 변경하고, 최근 측정된 measurement의 rtt가 양수이면 회항중인 세그먼트 수를
 * 계산한 뒤, RTT의 변동(fluctuation)을 고려해 3을 더한 뒤 cwnd로 세팅함.
 * inet_csk_ca는 sock *sk가 사용하는 congestion control의 정보를
 * inet_connection_sock의 구조체의 멤버 변수 icsk_ca_priv에서 가져다줌
 * http://www.yonch.com/tech/linux-tcp-congestion-control-internals
 * 를
 */
void dpstate::set_pacing_rate() {
    uint64_t segs_in_flight; /* desired cwnd as rate * rtt */
    
    // TODO: sk->sk_pacing_rate = rate; 중요

    // http://www.morenice.kr/74 참고
    if (smoothed.rtt > 0) {
        segs_in_flight = snd_rate * smoothed.rtt / ((uint64_t) MTU * S_TO_US);
        /* Add few more segments to segs_to_flight to prevent rate underflow due to
         * temporary RTT fluctuations. */
        snd_cwnd = segs_in_flight + 3;

        vlog << "ccp: Setting new rate " << snd_rate / 125000 << "Mbit/s (" << snd_rate << " Bps) (cwnd " << segs_in_flight + 3 << " )" << std::endl;
    }
}

int dpstate::rate_sample_valid(const struct rate_sample *rs)
{
  int ret = 0;
  if (rs->delivered <= 0)
    ret |= 1;
  if (rs->snd_int_us <= 0)
    ret |= 1 << 1;
  if (rs->rcv_int_us <= 0)
    ret |= 1 << 2;
  if (rs->interval_us <= 0)
    ret |= 1 << 3;
  if (rs->rtt_us <= 0)
    ret |= 1 << 4;
  return ret;
}

// hjroh new is replaced with __new due to keyword problem in C++
uint64_t dpstate::ewma(uint64_t old, uint64_t __new) {
    if (old == 0) {
        return __new;
    }

    return ((__new * CCP_EWMA_RECENCY) +
        (old * (CCP_FRAC_DENOM-CCP_EWMA_RECENCY))) / CCP_FRAC_DENOM;
}

void dpstate::congestion_control(const struct rate_sample *rs) {
    // aggregate measurement
    // state = fold(state, rs)
    // TODO custom fold functions (for now, default only all fields)
    
    // TODO: measure from the real state
    ccp_measurement curr_mmt = {
        .ackNo = snd_una,
        .rtt = (uint32_t) rs->rtt_us,
        .loss = (uint32_t) rs->losses,
        .rin = 0, /* send bandwidth in bytes per second */
        .rout = 0, /* recv bandwidth in bytes per second */
    };

    int measured_valid_rate = rate_sample_valid(rs);
    if (measured_valid_rate == 0) {
        curr_mmt.rin = curr_mmt.rout = (uint64_t) rs->delivered * MTU * S_TO_US;
        curr_mmt.rin = curr_mmt.rin / ((uint64_t) rs->snd_int_us); // hjroh
        curr_mmt.rout = curr_mmt.rout / ((uint64_t) rs->rcv_int_us); // hjroh
    } else {
        return;
    }

    vlog << "new measurement: ack " << curr_mmt.ackNo << ", rtt " << curr_mmt.rtt << ", rin " << curr_mmt.rin << ", rout " << curr_mmt.rout << std::endl;

    smoothed.ackNo = curr_mmt.ackNo; // max()
    smoothed.rtt = ewma(smoothed.rtt, curr_mmt.rtt);
    smoothed.rin = ewma(smoothed.rin, curr_mmt.rin);
    smoothed.rout = ewma(smoothed.rout, curr_mmt.rout);
    smoothed.loss = curr_mmt.loss;

    vlog << "curr measurement: ack " << smoothed.ackNo << ", rtt " << smoothed.rtt << ", rin " << smoothed.rin << ", rout " << smoothed.rout << std::endl;

    // rate control state machine
    sync_with_agent(get_now());
}

uint32_t dpstate::get_ssthresh() {
    return std::max<uint32_t>(snd_cwnd >> 1, (uint32_t) 2);
}

uint32_t dpstate::undo_cwnd() {
    return std::max<uint32_t>(snd_cwnd, snd_ssthresh << 1);
}

void dpstate::pkts_acked(const struct ack_sample *sample) {
    int32_t sampleRTT = sample->rtt_us;
    vlog << "Just FYI: pkt sample rtt " << sampleRTT << " us" << std::endl;
}


/*
 * Detect drops.
 *
 * TCP_CA_Loss -> a timeout happened
 * TCP_CA_Recovery -> an isolated loss (3x dupack) happened.
 * TCP_CA_CWR -> got an ECN
 */
void dpstate::set_state(uint8_t new_state) {
    enum drop_type dtype;
    switch (new_state) { // tcp_ca_state
        case TCP_CA_Recovery:
        	vlog << "entered TCP_CA_Recovery (dupack drop)\n";
            dtype = DROP_DUPACK;
            break;
        case TCP_CA_Loss:
            vlog << "entered TCP_CA_Loss (timeout drop)\n";
            dtype = DROP_TIMEOUT;
            break;
        case TCP_CA_CWR:
            vlog << "entered TCP_CA_CWR (ecn drop)\n";
            dtype = DROP_ECN;
            break;
        default:
        	vlog << "entered TCP normal state\n";
            last_drop_state = NO_DROP;
            return;
    }

    if (last_drop_state == dtype) {
        return;
    }

    last_drop_state = dtype;
    // TODO: nl_send_drop_notif(socketId, dtype);
}

void dpstate::print_log() {
    std::cout << vlog.str() << std::endl;vlog.clear();
}

void dpstate::set_socketId(sockid socketId) {
    this->socketId = socketId;
}

sockid dpstate::get_socketId() {
    return socketId;
}

dp_time dpstate::get_now() {
    //http://egloos.zum.com/sweeper/v/2996847
    //https://stackoverflow.com/questions/28964547/cast-chronomilliseconds-to-uint64-t
    // http://rachelnertia.github.io/programming/2018/01/07/intro-to-std-chrono/
    std::chrono::microseconds mus = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch());
    now_time = mus.count();
    return now_time;
};

dp_time dpstate::get_mus_to_dp_time_fn() {
    return 1;
    //TODO: 
    return mus_time; 
};
} // namespace quic;