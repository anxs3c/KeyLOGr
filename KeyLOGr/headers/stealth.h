#ifndef STEALTH_H
#define STEALTH_H

int hide_process(const char *proc_name);
int hide_file(const char *file_path);
int is_debugger_present();
int is_virtualized();
int check_analysis_env();
void setup_stealth();
int daemonize();

#endif