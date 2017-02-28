#include<muduo/base/Logging.h>
#include<muduo/base/Timestamp.h>
#include<muduo/net/EventLoop.h>
#include<muduo/net/InetAddress.h>
#include<muduo/net/TcpClient.h>
#include<muduo/net/TcpServer.h>
#include"_HttpContext.h"


class Tunnel:public std::enable_shared_from_this<Tunnel>,
                    muduo::noncopyable
{
private:
    muduo::net::TcpClient client_;
    muduo::net::TcpConnectionPtr serverConn_;
    muduo::net::TcpConnectionPtr clientConn_;

    //拆卸连接
    void teardown()
    {
        clientConn_->setConnectionCallback(muduo::net::defaultConnectionCallback);
        client_.setMessageCallback(muduo::net::defaultMessageCallback);
        if(serverConn_)
        {
            serverConn_->setContext(boost::any());
            serverConn_->shutdown();
        }
        clientConn_.reset();
    }
    //连接回调函数
    void onClientConnection(const muduo::net::TcpConnectionPtr& conn)
    {
        using namespace std::placeholders;
        LOG_DEBUG<<(conn->connected()?"UP":"DOWN");
        if(conn->connected())
        {
            //连接成功之后：
            conn->setTcpNoDelay(true);
            conn->setHighWaterMarkCallback(
                std::bind(&Tunnel::onHighWaterMarkWeak,std::weak_ptr<Tunnel>(shared_from_this()),
                            kClient,_1,_2),1024*1024);
            //设置关联，与serverConn与conn相关联
            serverConn_->setContext(conn);
            LOG_INFO<<"SET CONNECTION CONTEXT";
            //开始读客户端发来的数据
            //serverConn_->startRead();
            clientConn_ = conn;
            /*
            if(serverConn_->inputBuffer()->readableBytes()>0)
            {
                //如果serverConn的inputBuffer中有数据，就立即发送给后端服务器
                //这里在今后需要增加一个解码器，只有消息接收完整之后再转发给后端服务器
                conn->send(serverConn_->inputBuffer());
            }*/
            std::shared_ptr<_HttpContext>context(new _HttpContext());
            std::pair<bool,int> info;
            if(serverConn_->inputBuffer()->readableBytes()>0
                &&(info = context->parseRequest(serverConn_->inputBuffer())).first)  //解析请求是完整的
            {
                LOG_INFO<<"info.second: "<<info.second;
                clientConn_->send(serverConn_->inputBuffer()->peek(),info.second);
                context->reset();
            }
        }
        else
        {
            teardown();
        }
    }
    //收到消息后的回调
    void onClientMessage(const muduo::net::TcpConnectionPtr& conn,
                        muduo::net::Buffer*buf, muduo::Timestamp)
    {
        LOG_DEBUG<<conn->name()<<" "<<buf->readableBytes();
        if(serverConn_)
        {
            //收到数据直接转发给客户端即可，因为不需要考虑客户端的性能
            serverConn_->send(buf);
        }
        else
        {
            //其实不存在这种情况，因为一定有serverConn_
            buf->retrieveAll();
            abort();
        }
    }

    enum ServerClient
    {
        kServer,kClient
    };
    //高水位回调
    void onHighWaterMark(ServerClient which,
                        const muduo::net::TcpConnectionPtr& conn,
                        size_t bytesToSent)
    {
        using namespace std::placeholders;
        
        LOG_INFO<<(which == kServer?"server":"client")
                <<"onHighWaterMark"<<conn->name()
                <<"bytes"<<bytesToSent;
        
        if(which == kServer)
        {
            //如果是与客户端建立的连接触发了高水位回调
            if(serverConn_->outputBuffer()->readableBytes()>0)
            {
                //停止从后端服务器读数据，因为从后端读得越多，写入客户端连接缓冲区的数据越多
                clientConn_->stopRead();
                //为与客户端的连接设置写完成回调
                serverConn_->setWriteCompleteCallback(
                    std::bind(&Tunnel::onWriteCompleteWeak,
                            std::weak_ptr<Tunnel>(shared_from_this()),kServer,_1));
            }
        }
        else
        {
            if(clientConn_->outputBuffer()->readableBytes()>0)
            {
                serverConn_->stopRead();
                clientConn_->setWriteCompleteCallback(
                    std::bind(&Tunnel::onWriteCompleteWeak,
                            std::weak_ptr<Tunnel>(shared_from_this()),kClient,_1));                
            }
        }
    }
    //写完成回调
    void onWriteComplete(ServerClient which,
                        const muduo::net::TcpConnectionPtr& conn)
    {
        LOG_INFO<<(which == kServer?"server":"client")
                <<"onHighWaterMark"<<conn->name()
                <<"bytes";
        
        if(which == kServer)
        {
            clientConn_->startRead();
            //将写完成回调设置为空，这样就不会每次写完都回调一次了
            serverConn_->setWriteCompleteCallback(muduo::net::WriteCompleteCallback());
        }      
        else
        {
            serverConn_->startRead();
            clientConn_->setWriteCompleteCallback(muduo::net::WriteCompleteCallback());
        }  
    }
    //两个弱回调，防止循环引用
    static void onHighWaterMarkWeak(const std::weak_ptr<Tunnel>&wkTunnel,
                                ServerClient which,
                                const muduo::net::TcpConnectionPtr& conn,
                                size_t bytesToSent)
    {
        std::shared_ptr<Tunnel>tunnel = wkTunnel.lock();
        if(tunnel)
        {
            tunnel->onHighWaterMark(which,conn,bytesToSent);
        }
    }

    static void onWriteCompleteWeak(const std::weak_ptr<Tunnel>&wkTunnel,
                                ServerClient which,
                                const muduo::net::TcpConnectionPtr& conn)
    {
        std::shared_ptr<Tunnel>tunnel = wkTunnel.lock();
        if(tunnel)
        {
            tunnel->onWriteComplete(which,conn);
        }
    }

public:
    //构造函数，做了客户端初始化，并且把与客户端连接传入（serverConn)
    Tunnel(muduo::net::EventLoop* loop,
            const muduo::net::InetAddress& serverAddr,
            const muduo::net::TcpConnectionPtr& serverConn)
        :client_(loop,serverAddr,serverConn->name()),
        serverConn_(serverConn)
    {
        LOG_INFO<<"Tunnel"<<serverConn->peerAddress().toIpPort()
                <<"<->"<<serverAddr.toIpPort();
    }

    ~Tunnel()
    {
        LOG_INFO<<"~Tunnel";
    }

    void setup()
    {
        using namespace std::placeholders;
        client_.setConnectionCallback(
            std::bind(&Tunnel::onClientConnection,shared_from_this(),_1));
        client_.setMessageCallback(
            std::bind(&Tunnel::onClientMessage,shared_from_this(),_1,_2,_3));
        serverConn_->setHighWaterMarkCallback(
            std::bind(&Tunnel::onHighWaterMarkWeak,std::weak_ptr<Tunnel>(shared_from_this()),kServer,_1,_2),1024*1024);
    }

    void connect()
    {
        client_.connect();
    }

    void disconnect()
    {
        client_.disconnect();
    }
};
typedef std::shared_ptr<Tunnel> TunnelPtr;