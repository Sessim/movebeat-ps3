#include <stdlib.h>
// C linkage: glibc provides memalign; the MSVC tests define it themselves.
#ifdef __cplusplus
extern "C"
#endif
void* memalign(size_t alignment, size_t size);
