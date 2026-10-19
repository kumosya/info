extern "C" int osinfoProbeBase();
extern "C" int osinfoProbeValue() { return osinfoProbeBase() + 6; }
