#ifndef INFO_MSG_H
#define INFO_MSG_H

const char HELLO_MSG[] =
"===============================================\n"
"          Wifi Headphones Manager CLI          \n"
"===============================================\n"
" Welcome! This tool allows you to manage the   \n"
" connection to your wireless wifi headphones.  \n"
"                                               \n"
" Use 'help' or '?' to view available commands. \n"
" Use 'exit' to exit the program.               \n"
"===============================================\n";

const char EXIT_MSG[] =
"Exiting from wifi headphones cli.              \n";

const char HELP_MSG[] =
"There are 6 commands available:                \n"
"  - status                                     \n"
"  - discovery <duration:opt>                   \n"
"  - discovery_data                             \n"
"  - connect <ip:req>                           \n"
"  - disconnect <ip:req>                        \n";

#endif /* INFO_MSG_H */
