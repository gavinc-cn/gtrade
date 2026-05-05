#pragma once

#include <QString>
#include <QDateTime>
#include <cstdint>

class DateTimeUtils {
public:
    // Convert nanoseconds timestamp to readable format
    static QString formatNanoseconds(int64_t nanoseconds);

    // Convert nanoseconds to milliseconds
    static int64_t nanosecondsToMilliseconds(int64_t nanoseconds);

    // Format datetime string
    static QString formatDateTime(const QDateTime& dt);
    static QString formatDateTime(const QString& isoString);

    // Get current time formatted
    static QString getCurrentTime();

    // Check if timestamp is old (for coloring)
    static bool isTimestampOld(const QString& timestamp, int thresholdSeconds);

private:
    DateTimeUtils() = default;
};
