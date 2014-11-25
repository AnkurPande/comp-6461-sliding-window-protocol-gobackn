#include <time.h>
#include <sys/types.h>
#include <sys/timeb.h>

class Timer
{
	struct _timeb t0, t1; // t0 is starting time, t1 is stopping time
	double interval;

public:
	Timer() {};
	~Timer() {};

	/* Set the timer interval (in milliseconds) */
	void SetInterval(int i)
	{
		_ftime64_s(&t0); // get the current time 64_s added
		interval = (double)i;
	}

	bool TimedOut()
	{
		double elapsedtime;
		_ftime64_s(&t1); // get the current time
		elapsedtime = difftime(t1.time, t0.time) * 1000. + t1.millitm - t0.millitm;
		return (elapsedtime >= interval);
	}
};
