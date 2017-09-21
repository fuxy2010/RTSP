#ifndef _RTSP_SERVER_H_
#define _RTSP_SERVER_H_

#include <iostream>
#include <string>
#include <list>

#ifdef RTSP_SERVER_CLASS_TYPE
#undef RTSP_SERVER_CLASS_TYPE
#endif

//��ʹ�þ�̬�⣬���ڳ���Ԥ����ѡ���м���궨��"_EXPORT_LIB"
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
		unsigned short		_uuid;//ת��ԴUUID
		unsigned short		_client_count;//��ǰ���Ӹ�ת��Դ�Ŀͻ�������
		std::list<double>	_bit_rate;//ת��Դ�����Ĵ�������, kbps
		unsigned long		_start_time;//��ת��Դ����ʱ��
	}RELAY_SOURCE_INFO, *RELAY_SOURCE_INFO_PTR;

	class RTSP_SERVER_CLASS_TYPE CRTSPServer
	{
	public:
		CRTSPServer(unsigned short rtsp_port = 554);
		~CRTSPServer();

		enum//ý��������
		{
			kVideoStream =	0,
			kAudioStream =	1,
			kWhiteBoard =	2,
		};

	public:
		//�õ���ǰRTSP�����˿�
		unsigned short get_rtsp_port() { return _rtsp_port; }

		//���øýӿڣ�����nMaxPacketSize���ɹ�С
		//�������ݰ����ߴ磨���ó���1400��
		void set_max_packet_size(unsigned short nMaxPacketSize = 1400);

		//��������
		int input_stream_data(unsigned short uuid, void* pData, unsigned long nLen, unsigned short nDataType);

		//��ѯRTSP�Ự��Ϣ
		int query_relay_source();

		//���ý���RTSPClient���ݵĻص�����
		void set_data_callback(void (CALLBACK* pDataCallBackFunc)(const char* data, const unsigned long& length));

	private:
		unsigned short _rtsp_port;
	};
}

#endif //__RTSP_SERVER_H__
