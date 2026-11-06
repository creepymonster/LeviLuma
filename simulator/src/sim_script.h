#pragma once
// Testskripte fuer den Simulator: steuern die echte Anwendung ueber Mausereignisse (SDL-Ereignisschlange)
// und Befehle an die simulierten Gateways und pruefen, was die Firmware-Oberflaeche anzeigt.
// Format: eine Anweisung je Zeile, '#' leitet Kommentare ein. Siehe simulator/README.md.
#include <stdint.h>
#include <string>
#include <vector>

class SimScript {
public:
  enum class Result { Running, Done, Failed };
  bool load(const std::string &path);
  Result step(); // je Durchlauf der Hauptschleife
  const std::string &error() const {
    return error_;
  }

private:
  struct Line {
    int no;
    std::vector<std::string> args;
  };
  Result exec(const Line &l); // Running = Anweisung noch nicht fertig
  Result fail(const Line &l, const std::string &msg);

  std::vector<Line> lines_;
  size_t pc_ = 0;
  uint32_t stepStart_ = 0;
  int phase_ = 0;
  std::string error_;
  std::string dir_;
};
