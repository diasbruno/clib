#ifndef CLIB_GITLAB_REGISTRY_H
#define CLIB_GITLAB_REGISTRY_H

#include "list/list.h"

list_t* gitlab_registry_fetch(const char *url);

#endif //CLIB_GITLAB_REGISTRY_H
