#include"_HttpContext.h"
#include<muduo/net/Buffer.h>
#include<muduo/base/Logging.h>
using namespace muduo;
using namespace muduo::net;

bool _HttpContext::processRequestLine(const char*begin,const char*end)
{
   // size_t length = 0;
    bool ok = true;
    /*
    判断请求行合法性以及存储请求url
    */
    //return std::pair<bool,size_t>(ok,length);
    return ok;
}

std::pair<bool,int> _HttpContext::parseRequest(Buffer*buf)
{
    bool ok = true;
    bool hasMore = true;
    unsigned int bodySize = 0;
    int length = 0;
    char* peek = (char*)(buf->peek());
    while(hasMore)
    {   //如果有更多信息
        //处于读取请求行的阶段
        if(state_ == kExpectRequestLine)    
        {
	    //LOG_INFO<<"state: RequestLine";
            char* crlf =  (char*)(buf->findCRLF(peek));  //找\r\n
            if(crlf)
            {
                //处理请求行
                ok = processRequestLine(/*buf->peek()*/peek,crlf);
                if(ok)
                {
                    //将readIndex后移到下一行
                    //buf->retrieveUntil(crlf+2);
                    peek = crlf+2;
                    state_ = kExpectHeaders;    //进入下一个阶段
                }
                else
                {
                    hasMore = false;    //不用再读了
                }
            }
            else
            {
                //请求行不完整，返回false
                ok = false;     
                hasMore = false;
            }
        }
        //处于读取头部阶段
        else if(state_ == kExpectHeaders)
        {
	        //LOG_INFO<<"state: parse Header";
            char* crlf =  (char*)(buf->findCRLF(peek));
            if(crlf)
            {
                char* colon = std::find(peek,crlf,':');
                if(colon != crlf)
                {
                    std::string contentLength(/*buf->peek()*/peek,colon);
                    if(contentLength == "Content-Length")
                    {
                        //获取body的大小
                        bodySize = std::atoi(std::string(colon+1,crlf).c_str());
                    }
                    //request_.addHeader(/*buf->peek()*/peek,colon,crlf);
                    peek = crlf+2;          //peek指向下一行的第一个字符
                }
                else
                {
                    //空行，证明Header结束了，开始body阶段
                    state_ = kExpectBody;
                    peek = peek + 2;    //peek指向body的第一行
                }
            }
            else
            {
		        //LOG_INFO<<"Header uncomplete";
                //头部不完整，返回false;
                ok = false;
                hasMore = false;
            }
        }
        //处于读取主体阶段
        else if(state_ == kExpectBody)
        {
            /*std::string s((char*)buf->peek(),peek);
            LOG_INFO<<"\n"<<s<<"\n";*/
	        LOG_INFO<<"state: parse  Body";
            length += bodySize;
            //存在body，但是可读的数据少于contentlength
            if(bodySize != 0 && buf->readableBytes()< bodySize)
            {
                ok = false;
            }
            hasMore = false;
        }
    }
    length += (peek-buf->peek());
    return std::pair<bool,int>(ok,length);
}
