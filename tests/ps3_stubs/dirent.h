#ifndef MOVEBEAT_PS3_STUB_DIRENT_H
#define MOVEBEAT_PS3_STUB_DIRENT_H

struct DIR;

struct dirent {
    char d_name[256];
};

DIR* opendir(const char* path);
dirent* readdir(DIR* directory);
int closedir(DIR* directory);

#endif
