#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdbool.h>
//#include <conio.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>

#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#ifdef __cplusplus
extern "C"{
#endif

char* strstrstr(char* str, char* text, char* rear);//变态级的函数,看了可能会崩溃
char* httppost(char* hostname, char* url, char* cs);//POST方法
char* httpget(const char* hostname, char* url);//GET方法
char* getip(char* hostname);//域名转换IP
int hextoint(char* hex);//16进制字符串转整数

#ifdef __cplusplus
}
#endif


char* httppost(const char* hostname, char* url, char* cs){
	// sock句柄
	int sockfd;
	struct sockaddr_in serveraddr;
	//两个是同一个类型，可混用，但是会警告
	//int addrlen = sizeof(serveraddr);
    socklen_t addrlen = sizeof(serveraddr);
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		//printf("创建网络连接失败---socket error!\n");
		return NULL;
	}
	//C4droid的GCC没有这个函数，编译会报错
	//bzero(&serveraddr, addrlen);
	//可以使用这个函数
	memset(&serveraddr,0,addrlen);
	serveraddr.sin_family = AF_INET;
	//端口80
	serveraddr.sin_port = htons(80);
	struct hostent* host;
	host = gethostbyname(hostname);
	if (host == NULL){
		printf("无法解析域名\n");
		close(sockfd);
		return NULL;
	}
	struct in_addr ip = *((struct in_addr *)host->h_addr);
	//printf("连接服务器:%s\n",inet_ntoa(ip));
	serveraddr.sin_addr = ip;
	if (connect(sockfd, (struct sockaddr*) & serveraddr, addrlen) < 0){
		//printf("连接到服务器失败,connect error!\n");
		close(sockfd);
		return NULL;
	}
	//因为懒，所以char[2048]
	//可以通过strlen获取参数长度来动态申请内存
	char postxyt[2048];
	char postsjlen[5];
	// 发送数据
	//32位要用 "%u" 经测试发现还是long最稳定
	//顺利通过 GCC，arm-linux-gcc，arm-linux-gnueabi-gcc，安卓jni 编译 无警告
#if __SIZEOF_LONG__ == 8
	sprintf(postsjlen, "%lu", strlen(cs));
#else
	sprintf(postsjlen, "%u", strlen(cs));
#endif
	memset(postxyt, 0, 2048);
	// 追加字符串（请求协议头）
	//个人感觉strcat函数比较低效
	//用char指针和strcpy函数会比较快
	sprintf(postxyt,"POST /%s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/x-www-form-urlencoded\r\nUser-Agent: Mozilla/4.0(compatible)\r\nContent-Length: %ld\r\n\r\n%s\r\n\r\n",url,hostname,strlen(cs),cs);
	//别问，问就是 “一切皆文件”
	/*
	if (write(sockfd, postxyt, strlen(postxyt)) == -1){
		//printf("发送失败！错误代码:%d，错误信息:%s\n", errno, strerror(errno));
		close(sockfd);
		return NULL;
	}*/
	if (send(sockfd, postxyt, strlen(postxyt),0) == -1){
		printf("%d，%s\n", errno, strerror(errno));
		close(sockfd);
		return NULL;
	}
	fd_set fds;
	struct timeval tv = { 3,0 }; //select等待3秒，3秒轮询，要非阻塞就置0
	//非阻塞需要循环
	FD_ZERO(&fds); //每次循环都要清空集合，否则不能检测描述符变化 
	FD_SET(sockfd, &fds); //添加描述符

	if (select(sockfd + 1, &fds, NULL, NULL, &tv) < 1){
		//非阻塞时使用continue;
		//-1错误
		//0无写入数据
		close(sockfd);
		return NULL;
	}
	
	if (FD_ISSET(sockfd, &fds)) { //测试sock是否可读，即是否网络上有数据
		char xyt[1024];
		char* xytzz = xyt;
		char* xytmaxlen = xyt + 1023;
		int readlen;
		while (readlen = read(sockfd, xytzz, 1)){
			if(*xytzz == '\n'){
				//printf("匹配到一个\\n\n");
				if(strncmp(xytzz - 3,"\r\n\r",3) == 0){
					*++xytzz = '\0';
					//printf("嗨呀，完全匹配，好开心\n");
					break;
				}
			}
			xytzz++;
			if(xytmaxlen == xytzz){
				//printf("什么吊毛协议头这么长\n");
				close(sockfd);
				return NULL;
			}
		}
		if(!readlen){
			//printf("服务器断开了连接，并未发送任何数据。\n");
			close(sockfd);
			return NULL;
		}
		//printf("%s\n--\n",xyt);
		char* xylen = strstrstr(xyt,"Content-Length: ","\n");
		char* xyzw;
		if(xylen == NULL){
			//如果读取正文长度失败则进入分段读取模式
			char hexlen[8];
			char* hex = hexlen;
			do{
				read(sockfd, hex, 1);
			}while(*hex++ != '\n');
			*hex = '\0';
			int rdlen = hextoint(hexlen);
			if(rdlen == 0){
				close(sockfd);
				return NULL;
			}
			//rdlen++;
			xyzw = (char*)malloc(rdlen + 2);//把\r\n读出来
			read(sockfd, xyzw, rdlen + 2);//方便下面读取区块信息
			//xyzw[rdlen] = '\0';
			//读取下一块内容
			while(1){
				hex = hexlen;
				do{
					read(sockfd, hex, 1);
				}while(*hex++ != '\n');
				*hex = '\0';
				int chlen = hextoint(hexlen);
				//判断当前区块是否为0
				if(chlen == 0)break;
				rdlen += chlen;
				xyzw = (char*)realloc(xyzw,rdlen + 2);
				char* xrzz = xyzw + rdlen - chlen;
				read(sockfd, xrzz, chlen + 2);
				//xyzw[rdlen] = '\0';
			}
			xyzw[rdlen] = '\0';
		}else{
			//得到正文长度，一次性读取 申请内存
			int xyzwlen = atoi(xylen);
			free(xylen);
			//printf("正文共%d字节\n",xyzwlen);
			xyzw = (char*)malloc(xyzwlen + 1);
			readlen = read(sockfd, xyzw, xyzwlen);//接受网络数据
			xyzw[readlen] = '\0';
			//printf("读取了%d字节\n",readlen);
		}
		close(sockfd);
		return xyzw;
	}
	close(sockfd);
	return NULL;
}

char* httpget(const char* hostname, char* url){
	// sock句柄
	int sockfd;
	struct sockaddr_in serveraddr;
	//两个是同一个类型，可混用，但是会警告
	//int addrlen = sizeof(serveraddr);
    socklen_t addrlen = sizeof(serveraddr);
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		//printf("创建网络连接失败---socket error!\n");
		return NULL;
	}
	//C4droid的GCC没有这个函数，编译会报错
	//bzero(&serveraddr, addrlen);
	//可以使用这个函数
	memset(&serveraddr,0,addrlen);
	serveraddr.sin_family = AF_INET;
	//端口80
	serveraddr.sin_port = htons(80);
	struct hostent* host;
	host = gethostbyname(hostname);
	if (host == NULL){
		//printf("无法解析域名\n");
		close(sockfd);
		return NULL;
	}
	struct in_addr ip = *((struct in_addr *)host->h_addr);
	//printf("ip:%s\n",inet_ntoa(ip));
	serveraddr.sin_addr = ip;
	if (connect(sockfd, (struct sockaddr*) & serveraddr, addrlen) < 0){
		//printf("连接到服务器失败,connect error!\n");
		close(sockfd);
		return NULL;
	}
	//因为懒，所以char[2048]
	//可以通过strlen获取参数长度来动态申请内存
	char postxyt[2048];
	memset(postxyt, 0, 2048);
	// 追加字符串（请求协议头）
	//个人感觉strcat函数比较低效
	//用char指针和strcpy函数会比较快
	sprintf(postxyt,"GET /%s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\n",url,hostname);
	//别问，问就是 “一切皆文件”
	/*
	if (write(sockfd, postxyt, strlen(postxyt)) == -1){
		//printf("发送失败！错误代码:%d，错误信息:%s\n", errno, strerror(errno));
		close(sockfd);
		return NULL;
	}*/
	if (send(sockfd, postxyt, strlen(postxyt),0) == -1){
		printf("%d%s\n", errno, strerror(errno));
		close(sockfd);
		return NULL;
	}
	fd_set fds;
	struct timeval tv = { 3,0 }; //select等待3秒，3秒轮询，要非阻塞就置0
	//非阻塞需要循环
	FD_ZERO(&fds); //每次循环都要清空集合，否则不能检测描述符变化 
	FD_SET(sockfd, &fds); //添加描述符

	if (select(sockfd + 1, &fds, NULL, NULL, &tv) < 1){
		//非阻塞时使用continue;
		//-1错误
		//0无写入数据
		close(sockfd);
		return NULL;
	}
	
	if (FD_ISSET(sockfd, &fds)) { //测试sock是否可读，即是否网络上有数据
		char xyt[1024];
		char* xytzz = xyt;
		char* xytmaxlen = xyt + 1023;
		int readlen;
		while (readlen = read(sockfd, xytzz, 1)){
			if(*xytzz == '\n'){
				//printf("匹配到一个\\n\n");
				if(strncmp(xytzz - 3,"\r\n\r",3) == 0){
					*++xytzz = '\0';
					//printf("嗨呀，完全匹配，好开心\n");
					break;
				}
			}
			xytzz++;
			if(xytmaxlen == xytzz){
				//printf("什么吊毛协议头这么长\n%s\n",xyt);
				close(sockfd);
				return NULL;
			}
		}

		if(!readlen){
			//printf("服务器断开了连接，并未发送任何数据。\n");
			close(sockfd);
			return NULL;
		}
		
		//printf("%s\n--\n",xyt);
		char* xylen = strstrstr(xyt,"Content-Length: ","\n");
		char* xyzw;
		if(xylen == NULL){
			//如果读取正文长度失败则进入分段读取模式
			char hexlen[8];
			char* hex = hexlen;
			do{
				read(sockfd, hex, 1);
			}while(*hex++ != '\n');
			*hex = '\0';
			int rdlen = hextoint(hexlen);
			if(rdlen == 0){
				close(sockfd);
				return NULL;
			}
			//rdlen++;
			xyzw = (char*)malloc(rdlen + 2);//把\r\n读出来
			read(sockfd, xyzw, rdlen + 2);//方便下面读取区块信息
			//xyzw[rdlen] = '\0';
			//读取下一块内容
			while(1){
				hex = hexlen;
				do{
					read(sockfd, hex, 1);
				}while(*hex++ != '\n');
				*hex = '\0';
				int chlen = hextoint(hexlen);
				//判断当前区块是否为0
				if(chlen == 0)break;
				rdlen += chlen;
				xyzw = (char*)realloc(xyzw,rdlen + 2);
				char* xrzz = xyzw + rdlen - chlen;
				read(sockfd, xrzz, chlen + 2);
				//xyzw[rdlen] = '\0';
			}
			xyzw[rdlen] = '\0';
		}else{
			//得到正文长度，一次性读取 申请内存
			int xyzwlen = atoi(xylen);
			free(xylen);
			//printf("正文共%d字节\n",xyzwlen);
			xyzw = (char*)malloc(xyzwlen + 1);
			readlen = read(sockfd, xyzw, xyzwlen);//接受网络数据
			xyzw[readlen] = '\0';
			//printf("读取了%d字节\n",readlen);
		}
		close(sockfd);
		return xyzw;
	}
	close(sockfd);
	return NULL;
}

char* strstrstr(char* str, char* front, char* rear){
	if(!str || !front || !rear)return NULL;//如果你不传NULL,我至于吗
	char* s;
	char* t;
	while(*str) {
		s = str;
		t = front;
		while (*s == *t) {
			s++;
			t++;
			if (!*t) {
				str = s;
				char* old = str;
				do{
					s = str;
					t = rear;
					while (*s == *t) {
						s++;
						t++;
						if (!*t) {
							int charlen = str - old;
							char* newstr = (char*)malloc(charlen + 1);
							strncpy(newstr, old, charlen);
							//使用Visual studio编程时会警告strncpy函数存在风险，使用strncpy_s替换之
							//strncpy_s(newstr, charlen + 1,old, charlen);
							newstr[charlen] = '\0';
							return newstr;
						}
					}
					str++;
				}while(*str);
				return NULL;
			}
		}
		str++;
	}
	return NULL;
}

char* getip(char* hostname) {
	//不多bb，面向百度编程
	struct hostent* host;
	host = gethostbyname(hostname);
	if (host == NULL){
		perror("cannot get host by hostname");
		return NULL;
	}
	return inet_ntoa(*((struct in_addr *)host->h_addr));
}

int hextoint(char * hex){
    int value = 0;
    while (*hex){
        if (*hex >= 'A' && *hex <= 'F')
            value = (*hex - 55) + 16 * value;
        else if (*hex >= 'a' && *hex <= 'f')
            value = (*hex - 87) + 16 * value;
        else if (*hex >= '0' && *hex <= '9')
            value = (*hex - 48) + 16 * value;
        else{
            return value;
        }
        hex++;
    }
    return value;
}

typedef struct String
{
	char *str;
	size_t len;
} String;

// md5
typedef struct
{
	unsigned int count[2];
	unsigned int state[4];
	unsigned char buffer[64];
} MD5_CTX;

#define F(x, y, z) ((x & y) | (~x & z))
#define G(x, y, z) ((x & z) | (y & ~z))
#define H(x, y, z) (x ^ y ^ z)
#define I(x, y, z) (y ^ (x | ~z))
#define ROTATE_LEFT(x, n) ((x << n) | (x >> (32 - n)))
#define FF(a, b, c, d, x, s, ac) \
  {                              \
    a += F(b, c, d) + x + ac;    \
    a = ROTATE_LEFT(a, s);       \
    a += b;                      \
  }
#define GG(a, b, c, d, x, s, ac) \
  {                              \
    a += G(b, c, d) + x + ac;    \
    a = ROTATE_LEFT(a, s);       \
    a += b;                      \
  }
#define HH(a, b, c, d, x, s, ac) \
  {                              \
    a += H(b, c, d) + x + ac;    \
    a = ROTATE_LEFT(a, s);       \
    a += b;                      \
  }
#define II(a, b, c, d, x, s, ac) \
  {                              \
    a += I(b, c, d) + x + ac;    \
    a = ROTATE_LEFT(a, s);       \
    a += b;                      \
  }

#ifdef __cplusplus
extern "C"
{
#endif
	void MD5Init(MD5_CTX * context);
	void MD5Update(MD5_CTX * context, unsigned char *input, unsigned int inputlen);
	void MD5Final(MD5_CTX * context, unsigned char digest[16]);
	void MD5Transform(unsigned int state[4], unsigned char block[64]);
	void MD5Encode(unsigned char *output, unsigned int *input, unsigned int len);
	void MD5Decode(unsigned int *output, unsigned char *input, unsigned int len);
	
#ifdef __cplusplus
}
#endif
// md5

// base64
// 填入base64自定义编码集，可用群内app生成
// const char base[] =
// {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890+/"};
static char base[65] = "Bja8hLR2f1iz/T7Ku5SVW9EUONCvg+rnQbJPy4oqel3psDkdXHAY0wxmZGIFM6ct";

static union
{
	struct
	{
		unsigned long a:6;
		unsigned long b:6;
		unsigned long c:6;
		unsigned long d:6;
	} Sdata;
	unsigned char c[3];
} Udata;

#ifdef __cplusplus
extern "C"
{
#endif
	char *Encbase64(const char *orgdata);	// base64加密
	char *Decbase64(char *orgdata);	// base64解密
	char *toHEX(const char *string);	// 转16进制
	void setbase(const char b[65]);	// 设置base编码集
	char *itoa(int num, char *str, int radix);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C"
{
#endif
	// RC4
	void swap(unsigned char *s1, unsigned char *s2);
	void re_S(unsigned char *S);
	void re_T(unsigned char *T, const char *key);
	void re_Sbox(unsigned char *S, unsigned char *T);
	void re_RC4(unsigned char *S, const char *key);
	String RC4(const String data, const char *key);
#ifdef __cplusplus
}
#endif

// base代码
void setbase(const char b[65])
{
	strncpy(base, b, 64);
}

char *itoa(int num, char *str, int radix)
{
	char index[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";	// 索引表
	unsigned unum;				// 存放要转换的整数的绝对值,转换的整数可能是负数
	int i = 0, j, k;			// i用来指示设置字符串相应位，转换之后i其实就是字符串的长度；转换后顺序是逆序的，有正负的情况，k用来指示调整顺序的开始位置;j用来指示调整顺序时的交换。

	// 获取要转换的整数的绝对值
	if (radix == 10 && num < 0)	// 要转换成十进制数并且是负数
	{
		unum = (unsigned)-num;	// 将num的绝对值赋给unum
		str[i++] = '-';			// 在字符串最前面设置为'-'号，并且索引加1
	}
	else
		unum = (unsigned)num;	// 若是num为正，直接赋值给unum

	// 转换部分，注意转换后是逆序的
	do
	{
		str[i++] = index[unum % (unsigned)radix];	// 取unum的最后一位，并设置为str对应位，指示索引加1
		unum /= radix;			// unum去掉最后一位

	}
	while (unum);				// 直至unum为0退出循环

	str[i] = '\0';				// 在字符串最后添加'\0'字符，c语言字符串以'\0'结束。

	// 将顺序调整过来
	if (str[0] == '-')
		k = 1;					// 如果是负数，符号不用调整，从符号后面开始调整
	else
		k = 0;					// 不是负数，全部都要调整

	char temp;					// 临时变量，交换两个值时用到
	for (j = k; j <= (i - 1) / 2; j++)	// 头尾一一对称交换，i其实就是字符串的长度，索引最大值比长度少1
	{
		temp = str[j];			// 头部赋值给临时变量
		str[j] = str[i - 1 + k - j];	// 尾部赋值给头部
		str[i - 1 + k - j] = temp;	// 将临时变量的值(其实就是之前的头部值)赋给尾部
	}

	return str;					// 返回转换后的字符串
}

char *toHEX(const char *string)
{
	char chs;
	char *ret;
	char *str;
	if (!string || (ret = str = (char *)malloc(strlen(string) * 2 + 1)) == NULL)
		return NULL;
	while (*string)
	{
		chs = (*string & 0XF0) >> 4;
		if (chs > 9)
			*str = chs - 10 + 'A';	// chs - 10 + 'A'
		else
			*str = chs + '0';
		str++;
		chs = *string & 0X0F;
		if (chs > 9)
			*str = chs - 10 + 'A';	// chs - 10 + 'A'
		else
			*str = chs + '0';
		str++;
		string++;
	}
	*str = '\0';
	return ret;
}

char *Encbase64(const char *orgdata)
{
	const char *p = NULL;
	char *ret = NULL;
	int tlen = 0;
	if (orgdata == NULL)
		return NULL;
	unsigned long orglen = strlen(orgdata);
	tlen = orglen / 3;
	if (tlen % 3 != 0)
		tlen++;
	tlen = tlen * 4;
	if ((ret = (char *)malloc(tlen + 1)) == NULL)
		return NULL;
	memset(ret, 0, tlen + 1);
	p = orgdata;
	tlen = orglen;

	int i = 0, j = 0;
	while (tlen > 0)
	{
		Udata.c[0] = Udata.c[1] = Udata.c[2] = 0;
		for (i = 0; i < 3; i++)
		{
			if (tlen < 1)
				break;
			Udata.c[i] = (char)*p;
			tlen--;
			p++;
		}
		if (i == 0)
			break;
		switch (i)
		{
		case 1:
			/* ret[j++]=base[Udata.Sdata.d]; ret[j++]=base[Udata.Sdata.c];
			   ret[j++]=base[64]; ret[j++]=base[64]; */
			ret[j++] = base[Udata.c[0] >> 2];
			ret[j++] = base[((Udata.c[0] & 0x03) << 4) | ((Udata.c[1] & 0xf0) >> 4)];
			ret[j++] = base[64];
			ret[j++] = base[64];
			break;
		case 2:
			/* ret[j++]=base[Udata.Sdata.d]; ret[j++]=base[Udata.Sdata.c];
			   ret[j++]=base[Udata.Sdata.b]; ret[j++]=base[64]; */
			ret[j++] = base[Udata.c[0] >> 2];
			ret[j++] = base[((Udata.c[0] & 0x03) << 4) | ((Udata.c[1] & 0xf0) >> 4)];
			ret[j++] = base[((Udata.c[1] & 0x0f) << 2) | ((Udata.c[2] & 0xc0) >> 6)];
			ret[j++] = base[64];
			break;
		case 3:
			/* ret[j++]=base[Udata.Sdata.d]; ret[j++]=base[Udata.Sdata.c];
			   ret[j++]=base[Udata.Sdata.b]; ret[j++]=base[Udata.Sdata.a]; */
			ret[j++] = base[Udata.c[0] >> 2];
			ret[j++] = base[((Udata.c[0] & 0x03) << 4) | ((Udata.c[1] & 0xf0) >> 4)];
			ret[j++] = base[((Udata.c[1] & 0x0f) << 2) | ((Udata.c[2] & 0xc0) >> 6)];
			ret[j++] = base[Udata.c[2] & 0x3f];
			break;
		default:
			break;
		}
	}
	ret[j] = '\0';
	return ret;
}

char *Decbase64(char *orgdata)
{
	char *p, *ret;
	int len;
	unsigned long orglen;
	char ch[4] = { 0 };
	char *pos[4];
	int offset[4];
	if (orgdata == NULL)
		return NULL;
	orglen = strlen(orgdata);
	len = orglen * 3 / 4;
	if ((ret = (char *)malloc(len + 1)) == NULL)
		return NULL;
	p = orgdata;
	len = orglen;
	int j = 0;

	while (len > 0)
	{
		int i = 0;
		while (i < 4)
		{
			if (len > 0)
			{
				ch[i] = *p;
				p++;
				len--;
				if ((pos[i] = (char *)strchr(base, ch[i])) == NULL)
				{
					if (ch[i] == '=')
					{
						offset[i] = 0;
						i++;
						continue;
						// break;
					}
					free(ret);
					return NULL;
				}
				offset[i] = pos[i] - base;
			}
			i++;
		}
		if (ch[0] == '=' || ch[1] == '=' || (ch[2] == '=' && ch[3] != '='))
		{
			free(ret);
			return NULL;
		}
		ret[j++] = (unsigned char)(offset[0] << 2 | offset[1] >> 4);
		ret[j++] = offset[2] == 64 ? '\0' : (unsigned char)(offset[1] << 4 | offset[2] >> 2);
		ret[j++] = offset[3] == 64 ? '\0' : (unsigned char)((offset[2] << 6 & 0xc0) | offset[3]);
	}
	ret[j] = '\0';
	return ret;
}

// md5代码区

unsigned char PADDING[] = { 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// 定制md5

void MD5Init(MD5_CTX * context)
{
	context->count[0] = 0;
	context->count[1] = 0;
	context->state[0] = 0x67452301;
	context->state[1] = 0xEFCDAB89;
	context->state[2] = 0x98BADCFE;
	context->state[3] = 0x10325476;
}

void MD5Update(MD5_CTX * context, unsigned char *input, unsigned int inputlen)
{
	unsigned int i = 0, index = 0, partlen = 0;
	index = (context->count[0] >> 3) & 0x3F;
	partlen = 64 - index;
	context->count[0] += inputlen << 3;
	if (context->count[0] < (inputlen << 3))
		context->count[1]++;
	context->count[1] += inputlen >> 29;

	if (inputlen >= partlen)
	{
		memcpy(&context->buffer[index], input, partlen);
		MD5Transform(context->state, context->buffer);
		for (i = partlen; i + 64 <= inputlen; i += 64)
			MD5Transform(context->state, &input[i]);
		index = 0;
	}
	else
	{
		i = 0;
	}
	memcpy(&context->buffer[index], &input[i], inputlen - i);
}

void MD5Final(MD5_CTX * context, unsigned char digest[16])
{
	unsigned int index = 0, padlen = 0;
	unsigned char bits[8];
	index = (context->count[0] >> 3) & 0x3F;
	padlen = (index < 56) ? (56 - index) : (120 - index);
	MD5Encode(bits, context->count, 8);
	MD5Update(context, PADDING, padlen);
	MD5Update(context, bits, 8);
	MD5Encode(digest, context->state, 16);
}

void MD5Encode(unsigned char *output, unsigned int *input, unsigned int len)
{
	unsigned int i = 0, j = 0;
	while (j < len)
	{
		output[j] = input[i] & 0xFF;
		output[j + 1] = (input[i] >> 8) & 0xFF;
		output[j + 2] = (input[i] >> 16) & 0xFF;
		output[j + 3] = (input[i] >> 24) & 0xFF;
		i++;
		j += 4;
	}
}

void MD5Decode(unsigned int *output, unsigned char *input, unsigned int len)
{
	unsigned int i = 0, j = 0;
	while (j < len)
	{
		output[i] = (input[j]) | (input[j + 1] << 8) | (input[j + 2] << 16) | (input[j + 3] << 24);
		i++;
		j += 4;
	}
}

void MD5Transform(unsigned int state[4], unsigned char block[64])
{
	unsigned int a = state[0];
	unsigned int b = state[1];
	unsigned int c = state[2];
	unsigned int d = state[3];
	unsigned int x[64];
	MD5Decode(x, block, 64);
	FF(a, b, c, d, x[0], 7, 0xd76aa478);
	FF(d, a, b, c, x[1], 12, 0xe8c7b756);
	FF(c, d, a, b, x[2], 17, 0x242070db);
	FF(b, c, d, a, x[3], 22, 0xc1bdceee);
	FF(a, b, c, d, x[4], 7, 0xf57c0faf);
	FF(d, a, b, c, x[5], 12, 0x4787c62a);
	FF(c, d, a, b, x[6], 17, 0xa8304613);
	FF(b, c, d, a, x[7], 22, 0xfd469501);
	FF(a, b, c, d, x[8], 7, 0x698098d8);
	FF(d, a, b, c, x[9], 12, 0x8b44f7af);
	FF(c, d, a, b, x[10], 17, 0xffff5bb1);
	FF(b, c, d, a, x[11], 22, 0x895cd7be);
	FF(a, b, c, d, x[12], 7, 0x6b901122);
	FF(d, a, b, c, x[13], 12, 0xfd987193);
	FF(c, d, a, b, x[14], 17, 0xa679438e);
	FF(b, c, d, a, x[15], 22, 0x49b40821);

	GG(a, b, c, d, x[1], 5, 0xf61e2562);
	GG(d, a, b, c, x[6], 9, 0xc040b340);
	GG(c, d, a, b, x[11], 14, 0x265e5a51);
	GG(b, c, d, a, x[0], 20, 0xe9b6c7aa);
	GG(a, b, c, d, x[5], 5, 0xd62f105d);
	GG(d, a, b, c, x[10], 9, 0x2441453);
	GG(c, d, a, b, x[15], 14, 0xd8a1e681);
	GG(b, c, d, a, x[4], 20, 0xe7d3fbc8);
	GG(a, b, c, d, x[9], 5, 0x21e1cde6);
	GG(d, a, b, c, x[14], 9, 0xc33707d6);
	GG(c, d, a, b, x[3], 14, 0xf4d50d87);
	GG(b, c, d, a, x[8], 20, 0x455a14ed);
	GG(a, b, c, d, x[13], 5, 0xa9e3e905);
	GG(d, a, b, c, x[2], 9, 0xfcefa3f8);
	GG(c, d, a, b, x[7], 14, 0x676f02d9);
	GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

	HH(a, b, c, d, x[5], 4, 0xfffa3942);
	HH(d, a, b, c, x[8], 11, 0x8771f681);
	HH(c, d, a, b, x[11], 16, 0x6d9d6122);
	HH(b, c, d, a, x[14], 23, 0xfde5380c);
	HH(a, b, c, d, x[1], 4, 0xa4beea44);
	HH(d, a, b, c, x[4], 11, 0x4bdecfa9);
	HH(c, d, a, b, x[7], 16, 0xf6bb4b60);
	HH(b, c, d, a, x[10], 23, 0xbebfbc70);
	HH(a, b, c, d, x[13], 4, 0x289b7ec6);
	HH(d, a, b, c, x[0], 11, 0xeaa127fa);
	HH(c, d, a, b, x[3], 16, 0xd4ef3085);
	HH(b, c, d, a, x[6], 23, 0x4881d05);
	HH(a, b, c, d, x[9], 4, 0xd9d4d039);
	HH(d, a, b, c, x[12], 11, 0xe6db99e5);
	HH(c, d, a, b, x[15], 16, 0x1fa27cf8);
	HH(b, c, d, a, x[2], 23, 0xc4ac5665);

	II(a, b, c, d, x[0], 6, 0xf4292244);
	II(d, a, b, c, x[7], 10, 0x432aff97);
	II(c, d, a, b, x[14], 15, 0xab9423a7);
	II(b, c, d, a, x[5], 21, 0xfc93a039);
	II(a, b, c, d, x[12], 6, 0x655b59c3);
	II(d, a, b, c, x[3], 10, 0x8f0ccc92);
	II(c, d, a, b, x[10], 15, 0xffeff47d);
	II(b, c, d, a, x[1], 21, 0x85845dd1);
	II(a, b, c, d, x[8], 6, 0x6fa87e4f);
	II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
	II(c, d, a, b, x[6], 15, 0xa3014314);
	II(b, c, d, a, x[13], 21, 0x4e0811a1);
	II(a, b, c, d, x[4], 6, 0xf7537e82);
	II(d, a, b, c, x[11], 10, 0xbd3af235);
	II(c, d, a, b, x[2], 15, 0x2ad7d2bb);
	II(b, c, d, a, x[9], 21, 0xeb86d391);
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
}

// rc4加解密

// 一键加密
char *yjjm(const char *data);
// 校验函数

char *yjjm(const char *data)
{
	char *mw;
	data = Encbase64(data);
	mw = toHEX(data);
	//free(data);
	return mw;
}

/* 验证函数结束 */




