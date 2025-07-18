/**
 * nmrpflash - Netgear Unbrick Utility
 * Copyright (C) 2016 Joseph Lehner <joseph.c.lehner@gmail.com>
 *
 * nmrpflash is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nmrpflash is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with nmrpflash.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <unistd.h>
#include <getopt.h>
#include <locale.h>
#include <stdlib.h>
#include <locale.h>
#include <stdio.h>
#include <pcap.h>
#include "nmrpd.h"

#ifndef NMRPFLASH_WINDOWS
#include <sys/utsname.h>
#endif

int usage(FILE *fp)
{
	fprintf(fp,
			"Usage: nmrpflash [OPTIONS...]\n"
			"\n"
			"Options (-i, and -f or -c are mandatory):\n"
			" -a <ipaddr>     IP address to assign to target device [%s]\n"
			" -A <ipaddr>     IP address to assign to selected interface [%s]\n"
			" -B [<timeout>]  Blind mode. Initial timeout (seconds) [%d s]\n"
			" -c <command>    Command to run before (or instead of) TFTP upload\n"
			" -f <firmware>   Firmware file\n"
			" -F <filename>   Remote filename to use during TFTP upload\n"
			" -i <interface>  Network interface directly connected to device\n"
			" -m <mac>        MAC address of target device (xx:xx:xx:xx:xx:xx)\n"
			" -M <netmask>    Subnet mask to assign to target device [%s]\n"
			" -t <timeout>    Timeout (in milliseconds) for NMRP packets [%d ms]\n"
			" -T <timeout>    Time (seconds) to wait after successful TFTP upload [%d s]\n"
			" -p <port>       Port to use for TFTP upload [%d]\n"
#ifdef NMRPFLASH_SET_REGION
			" -R <region>     Set device region (NA, WW, GR, PR, RU, BZ, IN, KO, JP, AU)\n"
#endif
			" -S <n>          Skip <n> bytes of the firmware file\n"
			" -v              Be verbose\n"
			" -V              Print version and exit\n"
			" -L              List network interfaces\n"
			" -h              Show this screen\n"
			"\n"
			"Example: (run as "
#ifndef NMRPFLASH_WINDOWS
			"root"
#else
			"administrator"
#endif
			")\n\n"
#ifndef NMRPFLASH_WINDOWS
			"# nmrpflash"
#else
			"C:\\> nmrpflash.exe"
#endif
			" -i eth0 -f firmware.bin\n"
			"\n"
			"The command specified by -c will have environment variables IP, NETMASK, PORT,\n"
			"and MAC set to the router's IP address, subnet mask, TFTP port, and MAC address,\n"
			"respectively.\n"
			"\n"
			"nmrpflash %s, Copyright (C) 2016-2025 Joseph C. Lehner\n"
			"nmrpflash is free software, licensed under the GNU GPLv3.\n"
			"Source code at https://github.com/jclehner/nmrpflash\n"
			"\n"
			"%s\n",
			NMRP_DEFAULT_IP_REMOTE,
			NMRP_DEFAULT_IP_LOCAL,
			NMRP_DEFAULT_BLIND_TIMEOUT_S,
			NMRP_DEFAULT_SUBNET,
			NMRP_DEFAULT_RX_TIMEOUT_MS,
			NMRP_DEFAULT_UL_TIMEOUT_S,
			NMRP_DEFAULT_TFTP_PORT,
			NMRPFLASH_VERSION,
			pcap_lib_version()
	  );

	return fp == stderr ? 1 : 0;
}

#ifdef NMRPFLASH_WINDOWS
void require_admin()
{
	SID_IDENTIFIER_AUTHORITY auth = { SECURITY_NT_AUTHORITY };
	PSID group = NULL;
	BOOL admin, success = AllocateAndInitializeSid(
		&auth, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
		0, 0, 0, 0, 0, 0, &group
	);

	if (success) {
		success = CheckTokenMembership(NULL, group, &admin);
		FreeSid(group);
		if (success) {
			if (!admin) {
				fprintf(stderr, "Error: must be run as administrator\n");
				exit(1);
			} else {
				return;
			}
		}
	}

	fprintf(stderr, "Warning: failed to check administrator privileges\n");
}

void show_exit_prompt()
{
	DWORD pid;
	HWND win = GetConsoleWindow();
	if (!win || !GetWindowThreadProcessId(win, &pid)) {
		return;
	}

	if (GetCurrentProcessId() == pid) {
		printf("Press any key to exit\n");
		getch();
	}
}

// this is needed because atexit() expects cdecl, while WSACleanup() uses stdcall
void wsa_cleanup()
{
	WSACleanup();
}
#else
void require_admin()
{
	if (getuid() != 0) {
		fprintf(stderr, "Error: must be run as root\n");
		exit(1);
	}
}
#endif

void print_version()
{
	printf("nmrpflash %s", NMRPFLASH_VERSION);

#ifndef NMRPFLASH_WINDOWS
	struct utsname uts;
	if (uname(&uts) == 0) {
#ifdef NMRPFLASH_MACOS
		int maj, min;
		if (sscanf(uts.release, "%d.%d", &maj, &min) == 2) {
			if (maj >= 20) {
				printf(" on macOS %d.%d", maj - 9, min);
			} else {
				if (maj >= 16) {
					printf("on macOS");
				} else if (maj >= 12) {
					printf("on OS X");
				} else {
					printf("on Mac OS X");
				}

				printf(" 10.%d.%d", maj - 4, min);
			}

			printf(" (%s)\n", uts.machine);
			return;
		}
#endif
		printf(" on %s %s (%s)", uts.sysname, uts.release, uts.machine);
	}
#else
	printf(" on Windows");
	OSVERSIONINFO os;
	os.dwOSVersionInfoSize = sizeof(os);
	if (GetVersionEx(&os)) {
		printf(" %lu.%lu", os.dwMajorVersion, os.dwMinorVersion);
	}
	printf(" (%s)", getenv("PROCESSOR_ARCHITECTURE"));
#endif
	printf("\n");
}

int main(int argc, char **argv)
{
	int c, val, max;
	bool list = false, have_dest_mac = false;
	struct nmrpd_args args = {
		.rx_timeout = NMRP_DEFAULT_RX_TIMEOUT_MS,
		.ul_timeout = NMRP_DEFAULT_UL_TIMEOUT_S * 1000,
		.tftpcmd = NULL,
		.file_local = NULL,
		.file_remote = NULL,
		.ipaddr_intf = NULL,
		.ipaddr = NULL,
		.ipmask = NMRP_DEFAULT_SUBNET,
		.intf = NULL,
		.mac = "ff:ff:ff:ff:ff:ff",
		.op = NMRP_UPLOAD_FW,
		.port = NMRP_DEFAULT_TFTP_PORT,
		.region = NULL,
		.blind_timeout = 0,
		.offset = 0,
	};

	setlocale(LC_ALL, "en_US.UTF-8");
#ifdef NMRPFLASH_WINDOWS
	WSADATA wsa;

	SetConsoleOutputCP(CP_UTF8);
	SetThreadUILanguage(MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT));

	atexit(&show_exit_prompt);

	if (strstr(pcap_lib_version(), "WinPcap")) {
		fprintf(stderr, "Warning: WinPcap is no longer supported! Install Npcap instead.\n");
	}

	val = WSAStartup(MAKEWORD(2, 2), &wsa);
	if (val != 0) {
		win_perror2("WSAStartup", val);
		return 1;
	}

	atexit(&wsa_cleanup);

#ifndef _WIN64
	// This dirty hack works around the WOW64 file system redirector[1], which would prevent
	// us from calling programs residing in %windir%\System32 when running on a 64bit system
	// (since nmrpflash is currently shipped as 32bit only).
	//
	// [1] https://msdn.microsoft.com/en-us/library/windows/desktop/aa384187(v=vs.85).aspx

	char *oldpath = getenv("PATH");
	char *windir = getenv("WINDIR");
	if (oldpath && windir) {
		char *newpath = malloc(strlen(oldpath) + strlen(windir) + 32);
		sprintf(newpath, "%s;%s\\Sysnative", oldpath, windir);
		SetEnvironmentVariable("PATH", newpath);
		free(newpath);
	}
#endif
#endif

	opterr = 0;

	while ((c = getopt(argc, argv, ":a:A:Bc:f:F:i:m:M:p:R:S:t:T:hLVvU")) != -1) {
		switch (c) {
			case 'a':
				args.ipaddr = optarg;
				break;
			case 'A':
				args.ipaddr_intf = optarg;
				break;
			case 'c':
				args.tftpcmd = optarg;
				break;
			case 'f':
				args.file_local = optarg;
				break;
			case 'F':
				args.file_remote = optarg;
				break;
			case 'i':
				args.intf = optarg;
				break;
			case 'm':
				args.mac = optarg;
				have_dest_mac = true;
				break;
			case 'M':
				args.ipmask = optarg;
				break;
#ifdef NMRPFLASH_SET_REGION
			case 'R':
				args.region = optarg;
				break;
#endif
			case 'B':
			case 'p':
			case 'S':
			case 'T':
			case 't':
				if (c == 'p') {
					max = 0xffff;
				} else {
					max = 0x7fffffff;
				}

				if (optarg) {
					val = atoi(optarg);
					if (val <= 0 || val > max) {
						fprintf(stderr, "Invalid numeric value for -%c.\n", c);
						return 1;
					}
				} else if (c == 'B') {
					args.blind_timeout = NMRP_DEFAULT_BLIND_TIMEOUT_S;
					break;
				} else {
					fprintf(stderr, "Missing argument for -%c.\n", c);
					return 1;
				}

				if (c == 'B') {
					args.blind_timeout = val;
				} else if (c == 'p') {
					args.port = val;
				} else if (c == 't') {
					args.rx_timeout = val;
				} else if (c == 'T') {
					args.ul_timeout = val * 1000;
				} else if (c == 'S') {
					args.offset = val;
				}

				break;
			case 'V':
				print_version();
				return 0;
			case 'v':
				++verbosity;
				break;
			case 'L':
				list = true;
				break;
			case 'h':
				return usage(stdout);
			case ':':
				if (optopt == 'B') {
					args.blind_timeout = NMRP_DEFAULT_BLIND_TIMEOUT_S;
					break;
				}
				// fallthrough
			default:
				return usage(stderr);
		}
	}

	if (verbosity) {
		print_version();
	}

	if (args.ipaddr_intf && !args.ipaddr) {
		fprintf(stderr, "Error: cannot use -A <ipaddr> without using -a <ipaddr>.\n");
		return 1;
	}

	if (args.blind_timeout && !have_dest_mac) {
		fprintf(stderr, "Error: use of -B requires -m <mac>.\n");
		return 1;
	}

#ifndef NMRPFLASH_FUZZ
	if (!list && ((!args.file_local && !args.tftpcmd) || !args.intf)) {
		return usage(stderr);
	}

	if (!list) {
		require_admin();
	}
#endif
	if (list) {
		val = ethsock_list_all();
	} else {
		val = nmrp_do(&args);
		if (val != 0 && !g_interrupted && args.hints) {
			fprintf(stderr, "\n");
			if (args.hints & NMRP_MAYBE_FIRMWARE_INVALID) {
				fprintf(stderr,
						"Firmware file rejected by router. Possible causes:\n"
						"- Wrong firmware file (model number correct?)\n"
						"- Wrong file format (e.g. .chk vs .trx file)\n"
						"- Downgrading to a lower version number\n");
			} else if (args.hints & NMRP_NO_ETHERNET_CONNECTION) {
				fprintf(stderr,
						"No Ethernet connection detected. Possible causes:\n"
						"- Wrong Ethernet port - try others there's more than one\n"
						"- Bad Ethernet cable\n"
						"- Hardware issue\n");
			} else if ((args.hints & NMRP_NO_NMRP_RESPONSE) && !args.blind_timeout) {
				fprintf(stderr,
						"No response from router. Possible causes/fixes:\n"
						"- Unsupported router\n"
						"- Wrong Ethernet port - try others if there's more than one\n"
						"- Hold reset button for a few seconds while powering on router\n"
						"- Try blind mode (`-B` option)");
			} else if (args.hints & NMRP_TFTP_XMIT_BLK0_FAILURE) {
				fprintf(stderr,
						"Failed to send/receive initial TFTP packet. Possible fixes:\n"
						"- Disable firewall, or add an exception for nmrpflash\n"
						"- Manually specify IP addresses using `-a` and/or `-A`\n");
			}
		}
	}

	return val;
}
