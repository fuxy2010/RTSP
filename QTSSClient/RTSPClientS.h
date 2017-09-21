#ifndef _RTSP_CLIENT_S_H_
#define _RTSP_CLIENT_S_H_

#include <string>

#ifdef RTSP_CLIENT_CLASS_TYPE
	#undef RTSP_CLIENT_CLASS_TYPE
#endif

//如使用静态库，需在程序预处理选项中加入宏定义"_EXPORT_LIB"
#ifdef _EXPORT_LIB
	#define RTSP_CLIENT_CLASS_TYPE
#else
	#ifdef _EXPORT_DLL
		#define RTSP_CLIENT_CLASS_TYPE __declspec(dllexport)
	#else
		#define RTSP_CLIENT_CLASS_TYPE __declspec(dllimport)
	#endif
#endif

class ClientSession;

#ifndef _FOR_ACTIVE_X
#define _FOR_ACTIVE_X
#endif

class RTSP_CLIENT_CLASS_TYPE CRTSPClient
{
	public:
		CRTSPClient();
		~CRTSPClient();

		enum//RTSP Session媒体数据传输协议
		{
			kByTCP = 0,
			kByUDP = 1,
		};

		enum//媒体流类型
		{
			kVideoStream =	0,
			kAudioStream =	1,
			kWhiteBoard =	2,
		};

	public:
		int Connect(const char* url, int transport_mode = kByUDP);
		int Disconnect();

		int SetDataCallBack(void (CALLBACK* pDataCallBackFunc)(unsigned char* pData, int nLen, int nDataType, unsigned long nSequence, DWORD dwUserData),
							DWORD dwUserData);

		int SendDataPacket(const char* data, const unsigned short length, const unsigned char type);

	private:
		std::string _url;
		ClientSession* _session;

		//for Client
		void (CALLBACK* _data_call_back_func)(unsigned char* pData, int nLen, int nDataType, unsigned long nSequence, DWORD dwUserData);
		DWORD _user_data;

};

#endif //_RTSP_CLIENT_S_H_
