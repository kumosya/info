#include <iostream>
#include <unistd.h>

namespace std {

istream cin{STDIN_FILENO};
ostream cout{STDOUT_FILENO};
ostream cerr{STDERR_FILENO};

}  // namespace std
