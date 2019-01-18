# CCP-Chromium
QUIC Datapath Control Module for Chromium

## Introduction

Congestion Control Plane (CCP) for Chromium (`ccp-chromium`) is a implementation of *a*STEAM Project <https://asteam.korea.ac.kr>'s QUIC Datapath Control Module in the user space Chromium network stack compatible with `ccp-agent` <https://github.com/mit-nms/ccp> of Networks and Mobile Systems (NNS) group, MIT CSAIL. Although it is designed to integrate itself into QUIC Datapath, *the current published version* does not contain the integration code parts. Rather, it can operate as an independent process maintaining Transport Layer Protocol's Datapath state while communicating with `ccp-agent`.

## Requirements and Dependencies

* *An UNIX-like Operating System* which supports UNIX Domain Socket (UDS) Inter-Process Communication, since QUIC Datapath Control Module communicates with ccp-agent using UDS.
* *A standard C++11 Compiler*: `g++` is recommended
* `ccp-agent`: an experimental implementation of CCP written in the Go Programming Language, operated as an *a*STEAM Controller. <https://github.com/mit-nms/ccp>

## Instructions

* Build Instructions of `ccp-agent`
  1. Open a terminal
  2. Install the Go tools: Please refer <https://golang.org/doc/install>.
  3. Following 'How to Write Go Code' <https://golang.org/doc/code.html>, organize your Go workspace
  4. Go to `src` directory of your Go workspace: `cd $GOPATH/src`
  5. Make directories `github.com/mit-nms`: `mkdir -p github.com/mit-nms`
  6. Go to `mit-nms` directory: `cd github.com/mit-nms`
  7. Clone `ccp-agent`'s Git repository: `git clone https://github.com/mit-nms/ccp`
  8. Go to `ccp-agent`'s Git repository: `cd ccp`
  9. Install required dependencies of `ccp-agent`: `go get ./...`
  10. Build `ccp-agent`: `make`

* Build Instructions of `ccp-chromium`
  1. Open a terminal and go to an appropriate directory (We assume the directory is user's home directory `~`.)
  2. Clone this Git repository: `git clone https://github.com/ku-asteam/ccp-chromium.git`
  3. Go to the local Git repository: `cd ccp-chromium`
  4. Build the code with `make`: `make all`

* How to Run (In Case of TCP Reno Congestion Algorithm)
  1. `$GOPATH/src/github.com/mit-nms/ccp/ccpl --datapath=udp --congAlg=reno`
  2. `~/ccp-chromium/testccp`
