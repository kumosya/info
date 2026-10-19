extern "C" int osinfoMissingProbeSymbol();
extern "C" int osinfoProbeValue() { return osinfoMissingProbeSymbol(); }
