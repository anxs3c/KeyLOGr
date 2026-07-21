#ifndef INJECTION_H
#define INJECTION_H

int find_process(const char *name);
int attach_to_process(int pid);
void *get_function_address(void *handle, const char *name);
unsigned long find_code_cave(int pid, size_t size);
int inject_shellcode(int pid, void *shellcode, size_t shellcode_size);
int inject_dynamic_library(int pid, const char *lib_path);
int inject_into_process(int target_type);

#endif