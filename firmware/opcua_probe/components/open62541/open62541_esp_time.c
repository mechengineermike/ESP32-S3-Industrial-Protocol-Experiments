#include <sys/time.h>

#include "esp_timer.h"
#include "open62541.h"

UA_DateTime UA_DateTime_now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return UA_DATETIME_UNIX_EPOCH + ((UA_DateTime)tv.tv_sec * UA_DATETIME_SEC) +
           ((UA_DateTime)tv.tv_usec * 10);
}

UA_Int64 UA_DateTime_localTimeUtcOffset(void)
{
    return 0;
}

UA_DateTime UA_DateTime_nowMonotonic(void)
{
    return (UA_DateTime)esp_timer_get_time() * 10;
}
