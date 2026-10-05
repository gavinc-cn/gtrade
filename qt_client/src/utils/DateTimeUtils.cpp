#include "utils/DateTimeUtils.h"

QString DateTimeUtils::formatNanoseconds(int64_t nanoseconds) {
    // Convert nanoseconds to milliseconds
    int64_t milliseconds = nanoseconds / 1000000;

    QDateTime dt = QDateTime::fromMSecsSinceEpoch(milliseconds);
    return dt.toString("yyyy-MM-dd HH:mm:ss");
}

int64_t DateTimeUtils::nanosecondsToMilliseconds(int64_t nanoseconds) {
    return nanoseconds / 1000000;
}

QString DateTimeUtils::formatDateTime(const QDateTime& dt) {
    return dt.toString("yyyy-MM-dd HH:mm:ss");
}

QString DateTimeUtils::formatDateTime(const QString& isoString) {
    QDateTime dt = QDateTime::fromString(isoString, Qt::ISODate);
    if (!dt.isValid()) {
        return isoString; // Return original if can't parse
    }
    return formatDateTime(dt);
}

QString DateTimeUtils::getCurrentTime() {
    return QDateTime::currentDateTime().toString("HH:mm:ss");
}

bool DateTimeUtils::isTimestampOld(const QString& timestamp, int thresholdSeconds) {
    QDateTime dt = QDateTime::fromString(timestamp, "yyyy-MM-dd HH:mm:ss");
    if (!dt.isValid()) {
        return false;
    }

    qint64 secondsDiff = dt.secsTo(QDateTime::currentDateTime());
    return secondsDiff > thresholdSeconds;
}
