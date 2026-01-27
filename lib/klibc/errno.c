/*
 * errno variable definition
 * 
 * In a multi-threaded environment, this would need to be thread-local,
 * but for a simple freestanding environment, a global variable is sufficient.
 */
int errno = 0;
