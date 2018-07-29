#include "dpstate.h"

#include <string>

namespace quic {

dpstate::dpstate() {

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
    // TODO: ccp_set_pacing_rate
    return true;
}
bool dpstate::doSetRateRel(uint32_t factor) {
    uint64_t newrate = snd_rate * factor;
    newrate = newrate / 100;
    vlog << "rate -> " << newrate << std::endl;
    snd_rate = newrate;
    // TODO: ccp_set_pacing_rate;
    return true;
}

bool dpstate::doReport() {
    vlog << "report = (" << sample.ackNo << ", " << sample.rtt << ", " << sample.loss << ", " << sample.rin << ", " << sample.rout << ")" << std::endl;
    // TODO: nl_send_measurement;
    return true;
}

bool dpstate::doWaitAbs(uint32_t wait_us) {
    vlog << "waiting " << wait_us << " us" << std::endl;
    next_event_time = get_now() + wait_us * get_mus_to_dp_time_fn();
    return false;
}
bool dpstate::doWaitRel(uint32_t rtt_factor) {
    uint64_t rtt_us = sample.rtt;
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

void dpstate::set_agent(ctrlPath* agent) {
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

    if(get_now() > next_event_time) {
        currPatternEvent = (currPatternEvent + 1) % seq.size();
        vlog << "curr pattern event: " << currPatternEvent << std::endl;
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

}

bool dpstate::sync_from_agent(Events& seq, dp_time now) { // installPattern
    log_seq();
    this->seq = seq;
    currPatternEvent = seq.size() - 1;
    next_event_time = get_now();
    return sync_with_agent(now);
}

} // namespace quic;