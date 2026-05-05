#pragma once

class Bollinger {
public:
    explicit Bollinger(const size_t size=1): m_window_size(size) {}

    bool SetWindowSize(const size_t size) {
        if (size > m_window_size) {
            m_window_size = size;
            return true;
        }
        return false;
    }

    void SetStdMulti(const int multi) {
        m_std_multiple = multi;
    }

    void AddValue(const double value) {
        if (m_window.size() >= m_window_size) {
            const double oldValue = m_window.front();
            sum -= oldValue;
            sum_squares -= oldValue * oldValue;
            m_window.pop_front();
        }
        m_window.push_back(value);
        sum += value;
        sum_squares += value * value;
    }

    double GetAverage() const {
        return m_window.empty() ? 0.0 : sum / m_window.size();
    }

    double GetStdDev() const {
        if (m_window.empty()) return 0.0;
        const double mean = sum / m_window.size();
        const double variance = sum_squares / m_window.size() - mean * mean;
        return std::sqrt(variance);
    }

    double GetUpperBound() const {
        return GetAverage() + GetStdDev() * m_std_multiple;
    }

    double GetLowerBound() const {
        return GetAverage() - GetStdDev() * m_std_multiple;
    }

    void GetBollinger(double& upper, double& middle, double& lower) const {
        if (m_window.empty()) return;
        const double mean = sum / m_window.size();
        const double variance = sum_squares / m_window.size() - mean * mean;
        const double std_dev = std::sqrt(variance);
        middle = mean;
        upper = mean + std_dev * m_std_multiple;
        lower = mean - std_dev * m_std_multiple;
    }
private:
    size_t m_window_size;
    std::deque<double> m_window;
    double sum {};
    double sum_squares {};
    int m_std_multiple = 2;
};