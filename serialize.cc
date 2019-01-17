/*
Important Notice: Some parts of this source file contain Congestion Control
Plane Kernel Datapath written by mit-nms:
<https://github.com/mit-nms/ccp-kernel/blob/master/serialize.c>

The parts follow the MIT License as follows:

MIT License

Copyright (c) 2017 mit-nms

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include "serialize.h"

#include <cstring> // memcpy


namespace quic {

int readHeader(struct CcpMsgHeader *hdr, char *buf) {
    memcpy(hdr, buf, sizeof(struct CcpMsgHeader));
    switch (hdr->Type) {
    case CREATE:
    case MEASURE:
    case DROP:
    case PATTERN:
        return sizeof(struct CcpMsgHeader);
    default:
        return -1;
    }
}

int serializeHeader(char *buf, int bufsize, struct CcpMsgHeader *hdr) {
    switch (hdr->Type) {
    case CREATE:
    case MEASURE:
    case DROP:
    case PATTERN:
        break;
    default:
        return -1;
    }

    // if (bufsize < sizeof(struct CcpMsgHeader)) { // hjroh
    if ((unsigned long) bufsize < sizeof(struct CcpMsgHeader)) { // hjroh
        return -2;
    }

    memcpy(buf, hdr, sizeof(struct CcpMsgHeader));
    return sizeof(struct CcpMsgHeader);
}

/* buf is pointer to message buffer after the header has been written
 * bufsize is the remaining size in the buffer
 */
int serializeMeasureMsg(char *buf, int bufsize, struct MeasureMsg *msg) {
    //if (bufsize < sizeof(struct MeasureMsg)) { // hjroh
    if ((unsigned long) bufsize < sizeof(struct MeasureMsg)) { // hjroh
        return -2;
    }

    memcpy(buf, msg, bufsize);
    return sizeof(struct MeasureMsg);
}

int writeCreateMsg(
    char *buf,
    int bufsize,
    uint32_t sid,
    uint32_t startSeq,
    const char* congAlg
) {
    int ok;
    int congAlgLen = strlen(congAlg) + 1;
    struct CcpMsgHeader hdr = {
        .Type = CREATE,
        .Len = (uint8_t) (10 + congAlgLen),
        .SocketId = sid,
    };

    ok = serializeHeader(buf, bufsize, &hdr);
    if (ok < 0) {
        return ok;
    }

    // advance write head by header size
    buf += ok;

    memcpy(buf, &startSeq, sizeof(uint32_t));

    // advance write head by uint32_t size
    buf += sizeof(uint32_t);
    ok += sizeof(uint32_t);

    memset(buf, 0, congAlgLen);
    strncpy(buf, congAlg, congAlgLen);
    ok += congAlgLen;

    return ok;
}

int writeMeasureMsg(
    char *buf,
    int bufsize,
    uint32_t sid,
    uint32_t ackNo,
    uint32_t rtt,
    uint32_t loss,
    uint64_t rin,
    uint64_t rout
) {
    int ok;
    size_t ret;
    struct CcpMsgHeader hdr = {
        .Type = MEASURE,
        .Len = 34,
        .SocketId = sid,
    };

    struct MeasureMsg msg = {
        .ackNo = ackNo,
        .rtt = rtt,
        .loss = loss,
        .rin = rin,
        .rout = rout,
    };

    ok = serializeHeader(buf, bufsize, &hdr);
    if (ok < 0) {
        return ok;
    }

    buf += ok;
    ret = ok;
    ok = serializeMeasureMsg(buf, bufsize - ok, &msg);
    if (ok < 0) {
        return -2;
    }

    return ret + ok;
}

int writeDropMsg(
    char *buf,
    int bufsize,
    uint32_t sid,
    const char* str
) {
    int ok;
    int dropMsgLen = strlen(str) + 1;
    struct CcpMsgHeader hdr = {
        .Type = DROP,
        .Len = (uint8_t) (6 + dropMsgLen),
        .SocketId = sid,
    };

    // if (bufsize < sizeof(struct CcpMsgHeader) + dropMsgLen) { // hjroh
    if ((unsigned long) bufsize < sizeof(struct CcpMsgHeader) + dropMsgLen) {
        return -1;
    }

    ok = serializeHeader(buf, bufsize, &hdr);
    if (ok < 0) {
        return ok;
    }

    // advance write head by header size
    buf += ok;

    memset(buf, 0, dropMsgLen);
    strncpy(buf, str, dropMsgLen);

    return ok + dropMsgLen;
}

int readMsg(
    struct CcpMsgHeader *hdr,
    struct PatternMsg *msg,
    char *buf
) {
    int ok;
    ok = readHeader(hdr, buf);
    if (ok < 0) {
        return ok;
    }

    buf += ok;
    if (hdr->Type != PATTERN) {
        return -1;
    }

    memcpy(msg, buf, hdr->Len - 6);
    return hdr->Len;
}

int readPattern(Events& seq, char *pattern, int numEvents) {
    int i;
    seq.clear(); // 클리어하는 게 맞는지 체크 필요
    for (i = 0; i < numEvents; i++) {
        seq.push_back( *( (PatternEvent*) pattern) ); // 캐스팅 제대로 되는지 확인 필요

        if (seq[i].size == 2 && seq[i].type == REPORT) {
            pattern += 2;
            seq[i].val = 0;
            continue;
        } else if (seq[i].size != 6) {
            // only report events are 2 bytes
            // all other events are 6 bytes
            return -1;
        }

        pattern += seq[i].size;
    }

    return 0;
}

} // namespace quic