#ifndef _RTSP_SERVER_H_
#define _RTSP_SERVER_H_

#include <iostream>
#include <string>
#include <list>

#ifdef RTSP_SERVER_CLASS_TYPE
#undef RTSP_SERVER_CLASS_TYPE
#endif

//如使用静态库，需在程序预处理选项中加入宏定义"_EXPORT_LIB"
#ifdef _EXPORT_LIB
	#define RTSP_SERVER_CLASS_TYPE
#else
	#ifdef _EXPORT_DLL
		#define RTSP_SERVER_CLASS_TYPE __declspec(dllexport)
	#else
		#define RTSP_SERVER_CLASS_TYPE __declspec(dllimport)
	#endif
#endif

namespace RTSPServerLib
{
	typedef struct tagRELAY_SOURCE_INFO
	{
		unsigned short		_uuid;//转发源UUID
		unsigned short		_client_count;//当前连接该转发源的客户端总数
		std::list<double>	_bit_rate;//转发源各流的传输码率, kbps
		unsigned long		_start_time;//该转发源启动时间
	}RELAY_SOURCE_INFO, *RELAY_SOURCE_INFO_PTR;

	class RTSP_SERVER_CLASS_TYPE CRTSPServer
	{
	public:
		CRTSPServer(unsigned short rtsp_port = 554);
		~CRTSPServer();

		enum//媒体流类型
		{
			kVideoStream =	0,
			kAudioStream =	1,
			kWhiteBoard =	2,
		};

	public:
		//得到当前RTSP监听端口
		unsigned short get_rtsp_port() { return _rtsp_port; }

		//慎用该接口！！！nMaxPacketSize不可过小
		//设置数据包最大尺寸（不得超过1400）
		void set_max_packet_size(unsigned short nMaxPacketSize = 1400);

		//输入数据
		int input_stream_data(unsigned short uuid, void* pData, unsigned long nLen, unsigned short nDataType);

		//查询RTSP会话信息
		int query_relay_source();

		//设置接收RTSPClient数据的回调函数
		void set_data_callback(void (CALLBACK* pDataCallBackFunc)(const char* data, const unsigned long& length));

	private:
		unsigned short _rtsp_port;
	};
}

#endif //__RTSP_SERVER_H__
