#include "TestLoop.h"
#include "LoopPool.h"
#include "TcpConn.h"
#include "HttpTest.h"
#include "WsTest.h"
#include "ProtoDemuxer.h"

#include <string.h>

TestLoop::TestLoop(LoopPool* loopPool, PollType poll_type)
: loop_(new EventLoop(poll_type))
, loopPool_(loopPool)
, thread_()
{
    
}

void TestLoop::cleanup()
{
    std::lock_guard<std::mutex> lg(obj_mutex_);
    for (auto &kv : obj_map_) {
        kv.second->close();
        delete kv.second;
    }
    obj_map_.clear();
}

bool TestLoop::init()
{
    try {
        thread_ = std::thread([this] {
            run();
        });
    }
    catch(...)
    {
        return false;
    }
    return true;
}

void TestLoop::stop()
{
    //cleanup();
    if(loop_) {
        loop_->runInEventLoop([this] { cleanup(); });
        loop_->stop();
    }
    if(thread_.joinable()) {
        try {
            thread_.join();
        } catch (...) {
            printf("failed to join loop thread\n");
        }
    }
}

void TestLoop::run()
{
    if(!loop_->init()) {
        printf("TestLoop::run, failed to init EventLoop\n");
        return;
    }
    loop_->loop();
}

void TestLoop::addFd(SOCKET_FD fd, Proto proto)
{
    loop_->runInEventLoop([=] {
        switch (proto) {
            case PROTO_TCP:
            {
                long conn_id = loopPool_->getConnId();
                TcpConn* conn = new TcpConn(this, conn_id);
                addObject(conn_id, conn);
                conn->attachFd(fd);
                break;
            }
            case PROTO_HTTP:
            case PROTO_HTTPS:
            {
                long conn_id = loopPool_->getConnId();
                HttpTest* http = new HttpTest(this, conn_id);
                addObject(conn_id, http);
                http->attachFd(fd, proto==PROTO_HTTPS?SSL_ENABLE:0);
                break;
            }
            case PROTO_WS:
            case PROTO_WSS:
            {
                long conn_id = loopPool_->getConnId();
                WsTest* ws = new WsTest(this, conn_id);
                addObject(conn_id, ws);
                ws->attachFd(fd, proto==PROTO_WSS?SSL_ENABLE:0);
                break;
            }
            case PROTO_AUTO:
            case PROTO_AUTOS:
            {
                long conn_id = loopPool_->getConnId();
                ProtoDemuxer* demuxer = new ProtoDemuxer(this, conn_id);
                addObject(conn_id, demuxer);
                demuxer->attachFd(fd, proto==PROTO_AUTOS?SSL_ENABLE:0);
                break;
            }
                
            default:
                break;
        }
    });
}

#ifdef KUMA_OS_WIN
# define strcasecmp _stricmp
#endif

void TestLoop::addHttp(TcpSocket&& tcp, HttpParser&& parser)
{
    long conn_id = loopPool_->getConnId();
    HttpTest* http = new HttpTest(this, conn_id);
    addObject(conn_id, http);
    http->attachSocket(std::move(tcp), std::move(parser));
}

void TestLoop::addHttp2(TcpSocket&& tcp, HttpParser&& parser)
{
    H2Connection *h2conn = new H2Connection(loop_);
    h2conn->attachSocket(std::move(tcp), std::move(parser));
}

void TestLoop::addWebSocket(TcpSocket&& tcp, HttpParser&& parser)
{
    long conn_id = loopPool_->getConnId();
    WsTest* ws = new WsTest(this, conn_id);
    addObject(conn_id, ws);
    ws->attachSocket(std::move(tcp), std::move(parser));
}

void TestLoop::addObject(long conn_id, LoopObject* obj)
{
    std::lock_guard<std::mutex> lg(obj_mutex_);
    obj_map_.insert(std::make_pair(conn_id, obj));
}

void TestLoop::removeObject(long conn_id)
{
    printf("TestLoop::removeObject, conn_id=%ld\n", conn_id);
    std::lock_guard<std::mutex> lg(obj_mutex_);
    auto it = obj_map_.find(conn_id);
    if(it != obj_map_.end()) {
        delete it->second;
        obj_map_.erase(it);
    }
}

