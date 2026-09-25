#ifndef TIMER_H
#define TIMER_H
#include <time.h>
#include <string>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <unistd.h>
#include <thread>

struct timer {
	bool fpsio;
	int events;
	void* data;
	struct timespec str ,end,old,now,sleep,vsync,test,strloop ,endloop;
	long long  lodtime;
	long long  looptime;
	long long  runtime;
	std::string name;
	unsigned long Fps;
	long  SleepTime;
	long  vsyncSleepTime;
	unsigned long  old_time;
	unsigned long  now_time;
	long vsynctiemr[2];
	void setname(const char * name_){
		name = name_;
	}
	timer(char *in_name){
		name = in_name;
		SleepTime = 0;
		old_time = 0;
		lodtime = 0;
		now_time = 0;
		memset(&str,0, sizeof(str));
		memset(&end,0, sizeof(end));
		memset(&old,0, sizeof(old));
		memset(&now,0, sizeof(now));
		memset(&now,0, sizeof(sleep));
		memset(&now,0, sizeof(vsync));
	}
	timer(unsigned int fps){
		Fps = 1000000000/fps;
		SleepTime = 0;
		old_time = 0;
		lodtime = 0;
		now_time = 0;
		memset(&str,0, sizeof(str));
		memset(&end,0, sizeof(end));
		memset(&old,0, sizeof(old));
		memset(&now,0, sizeof(now));
		memset(&now,0, sizeof(sleep));
		memset(&now,0, sizeof(vsync));
	}
	timer(){
		SleepTime = 0;
		old_time = 0;
		lodtime = 0;
		now_time = 0;
		memset(&str,0, sizeof(str));
		memset(&end,0, sizeof(end));
		memset(&old,0, sizeof(old));
		memset(&now,0, sizeof(now));
		memset(&now,0, sizeof(sleep));
		memset(&now,0, sizeof(vsync));
	}
	
	//计时器开始
	inline void start(){
		clock_gettime(CLOCK_MONOTONIC,&str);
	}
	
	//计时器结束
	inline float stop(bool show){
		clock_gettime(CLOCK_MONOTONIC,&end);
		runtime = (((1000000000*end.tv_sec)+(end.tv_nsec))-((1000000000*str.tv_sec)+(str.tv_nsec)));
		if (show){
		}
		return runtime/1000000.0f;
	}
	
	// 使用 AotuFPS 之前 需调用 SetFps ，参数 单位 每秒循环次数
	inline void SetFps(unsigned int fps){
		Fps = 1000000000/fps;
	}
	
	inline void FpsEnd(){
		fpsio = false;
	}
	
	//初始化 AotuFPS ， 必须在 AotuFPS 之前调用
	inline void AotuFPS_init()
	{
		clock_gettime(CLOCK_MONOTONIC,&old);
		SleepTime = Fps;
		start();
	}
	
	long long oldtimer;
	//调用 AotuFPS 之前 请调用 AotuFPS_init  和 SetFps， SetFps 可实时调整FPS。此函数建议放在循环最前面。函数返回值为当前循环时间，单位毫秒， 1000.0f/返回值可得到准确fps
	inline float AotuFPS()
	{
		clock_gettime(CLOCK_MONOTONIC,&now);
		old_time = (((1000000000*now.tv_sec)+(now.tv_nsec))-((1000000000*old.tv_sec)+(old.tv_nsec)));
		SleepTime = Fps - old_time;
		if ( SleepTime < 0 ){
			SleepTime = 0;
			clock_gettime(CLOCK_MONOTONIC,&old);
			return (old_time+SleepTime)/1000000.0f;
		}
		nsleep(SleepTime);
		clock_gettime(CLOCK_MONOTONIC,&old);
		return (old_time+SleepTime)/1000000.0f;
	}

	//定时器单位毫秒，到达定时时间返回true，并且再次进入定时
	inline bool cktime(unsigned int ms)
	{
		if ( !lodtime ){
			start();
		}
		clock_gettime(CLOCK_MONOTONIC,&end);
		lodtime = (((1000000000*end.tv_sec)+(end.tv_nsec))-((1000000000*str.tv_sec)+(str.tv_nsec)))/1000;
		if ( lodtime >= ms ){
			lodtime = 0;
			return true;
		} else{
			return false;
		}
	}
	
	bool islooptimestart;
	inline void looptimestart()
	{
		islooptimestart = true;
		clock_gettime(CLOCK_MONOTONIC,&strloop);
	}
	
	inline void looptimeend()
	{
		islooptimestart = false;
	}
	
	inline long getlooptime()
	{
		clock_gettime(CLOCK_MONOTONIC,&endloop);
		looptime = (((1000000000*endloop.tv_sec)+(endloop.tv_nsec))-((1000000000*strloop.tv_sec)+(strloop.tv_nsec)));
		return looptime;
	}

	//setAffinity 辅助函数
	int32_t getNumCpus()
	{
		static int32_t sNumCpus = []() {
			pid_t pid = gettid();
			cpu_set_t cpuSet;
			CPU_ZERO(&cpuSet);
			sched_getaffinity(pid, sizeof(cpuSet), &cpuSet);
			int32_t numCpus = 0;
			while (CPU_ISSET(numCpus, &cpuSet)) {
				++numCpus;
			}
			return numCpus;
		}();
		return sNumCpus;
	}
	
	//设置CPU亲和，平稳循环时间 , 需要在循环同线程调用。
	void setAffinity()
	{
		const int32_t numCpus = getNumCpus();
		cpu_set_t cpuSet;
		CPU_ZERO(&cpuSet);
		for (int32_t cpu = 0; cpu < numCpus; ++cpu) {
			CPU_SET(cpu, &cpuSet);
		}
		sched_setaffinity(gettid(), sizeof(cpuSet), &cpuSet);
	}
	
	//高精度sleep,单位纳秒
	inline void nsleep(long delay)
	{
		struct timespec req, rem;
		long nano_delay = delay;
		while(nano_delay > 0)
		{
			rem.tv_sec = 0;
			rem.tv_nsec = 0;
			req.tv_sec = 0;
			req.tv_nsec = nano_delay;
			if((nanosleep(&req, &rem) == -1)){}
			nano_delay = rem.tv_nsec;
		};
		return ;
	}
};
#endif
