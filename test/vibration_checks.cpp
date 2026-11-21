#include <cstdio>
#include <cstring>
#include <string>

#include "MeasFormat.h"

static int failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static std::string sample(int32_t relUs, float ax, float ay, float az) {
  char buf[96];
  const std::size_t n = measfmt::formatSample(buf, sizeof buf, relUs, ax, ay, az);
  return std::string(buf, n);
}

static bool contains(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }

static void formatChecks() {
  CHECK(sample(-1000000, 0.5f, -1.25f, 9.8066f) == "-1000.000,0.500,-1.250,9.807\n");
  CHECK(sample(2000, 0, 0, 0) == "2.000,0.000,0.000,0.000\n");
  CHECK(sample(-2, 0, 0, 0) == "-0.002,0.000,0.000,0.000\n");
  CHECK(sample(0, 1, 2, 3).rfind("0.000,", 0) == 0);

  char path[32];
  CHECK(measfmt::measPath(7, path, sizeof path) == 15);
  CHECK(std::string(path) == "/MEAS/M0007.CSV");

  unsigned n = 0;
  CHECK(measfmt::parseMeasNumber("/MEAS/m0042.csv", n) && n == 42);
  CHECK(measfmt::parseMeasNumber("M0007.CSV", n) && n == 7);
  CHECK(!measfmt::parseMeasNumber("S0007.CSV", n));
  CHECK(!measfmt::parseMeasNumber("M007.CSV", n));
  CHECK(!measfmt::parseMeasNumber("M0007.TXT", n));

  CHECK(measfmt::nextMeasNumber(0) == 1);
  CHECK(measfmt::nextMeasNumber(41) == 42);
  CHECK(measfmt::nextMeasNumber(9998) == 9999);
  CHECK(measfmt::nextMeasNumber(9999) == 0);

  char buf[512];
  const measfmt::Header h{7, 20, 500.f, 8.f, 1.f, 3.f};
  const std::string header(buf, measfmt::formatHeader(buf, sizeof buf, h));
  bool allComments = !header.empty();
  for (std::size_t i = 0; i < header.size(); ++i)
    if ((i == 0 || header[i - 1] == '\n') && header[i] != '#') allComments = false;
  CHECK(allComments);
  CHECK(contains(header, "# measurement: 7\n"));
  CHECK(contains(header, "# threshold_m_s2: 2.0\n"));
  CHECK(contains(header, "# sample_rate_hz: 500.0\n"));
  CHECK(contains(header, "# range_g: 8\n"));
  CHECK(contains(header, "# pre_trigger_s: 1.0\n"));
  CHECK(contains(header, "# quiet_s: 3.0\n"));
  CHECK(contains(header, "# trigger_at: t_ms = 0\n"));
  const char *columns = "# columns: t_ms,ax_m_s2,ay_m_s2,az_m_s2\n";
  CHECK(header.size() >= std::strlen(columns) &&
        header.compare(header.size() - std::strlen(columns), std::string::npos, columns) == 0);

  const std::string footer(buf, measfmt::formatFooter(buf, sizeof buf, 1234, 5));
  CHECK(footer == "# end: samples=1234 dropped=5\n");
}

int main() {
  formatChecks();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("all ok");
  return 0;
}
