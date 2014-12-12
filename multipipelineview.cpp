//  multipipelineview
//
//  WEBSITE: https://github.com/JGRennison/multipipelineview
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program. If not, see <http://www.gnu.org/licenses/>.
//
//  2014 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <vector>
#include <string>

#ifndef VERSION_STRING
#define VERSION_STRING __DATE__ " " __TIME__
#endif
const char version_string[] = "multipipelineview " VERSION_STRING;
const char authors[] = "Written by Jonathan G. Rennison <j.g.rennison@gmail.com>";

#define READ_BUFFER_SIZE 4096

struct fdinfo {
	int fd;
	unsigned int line_number;
	std::string name;
	std::string current_line;
	std::string current_full_line;
	bool start_new_line = true;
};

bool force_exit = false;
bool resize_pending = true;
unsigned int line_count = 0;
unsigned int longest_name = 0;
std::vector<struct pollfd> pollfds;
std::vector<struct fdinfo> fdinfos;
struct winsize ws;

void getwinsize() {
	int result = ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
	if(result < 0) {
		fprintf(stderr, "Failed to get terminal size, %m\n");
		exit(1);
	}
}

unsigned int addpollfd(int fd, short events, std::string name, unsigned int line_number) {
	fdinfos.emplace_back();
	fdinfos.back().fd = fd;
	fdinfos.back().line_number = line_number;
	fdinfos.back().name = std::move(name);
	pollfds.push_back({ fd, events, 0 });
	return pollfds.size() - 1;
}

void delpollfd(unsigned int fd_offset) {
	//if slot is not the last one, move the last one in to fill empty slot
	if(fd_offset < pollfds.size() - 1) {
		fdinfos[fd_offset] = std::move(fdinfos.back());
		pollfds[fd_offset] = std::move(pollfds.back());
	}
	fdinfos.pop_back();
	pollfds.pop_back();
}

void setpollfdevents(unsigned int fd_offset, short events) {
	pollfds[fd_offset].events = events;
}

void setnonblock(int fd, const char *name) {
	int flags = fcntl(fd, F_GETFL, 0);
	int res = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	if(flags < 0 || res < 0) {
		fprintf(stderr, "Could not fcntl set O_NONBLOCK %s: %m\n", name);
		exit(1);
	}
}

void open_named_input(const char *input_name) {
	int fd = open(input_name, O_NONBLOCK | O_RDONLY);
	if(fd == -1) {
		//try to open the file as a Unix domain socket instead
		fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if(fd != -1) {
			setnonblock(fd, input_name);

			struct sockaddr_un my_addr;
			size_t maxlen = sizeof(my_addr.sun_path) - 1;
			memset(&my_addr, 0, sizeof(my_addr));
			my_addr.sun_family = AF_UNIX;
			if(strlen(input_name) > maxlen) {
				fprintf(stderr, "Socket name: %s too long, maximum: %d\n", input_name, (int) maxlen);
				close(fd);
				fd = -1;
			}
			else {
				strncpy(my_addr.sun_path, input_name, maxlen);
				if(connect(fd, (struct sockaddr *) &my_addr, sizeof(my_addr)) == -1) {
					close(fd);
					fd = -1;
				}
			}
		}
	}
	if(fd == -1) {
		fprintf(stderr, "Failed to open '%s' for input, %m. Exiting.\n", input_name);
		exit(1);
	}
	setnonblock(fd, input_name);
	unsigned int fd_offset = addpollfd(fd, POLLIN, input_name, line_count);
	fdinfo &info = fdinfos[fd_offset];
	if(info.name.size() > longest_name)
		longest_name = info.name.size();

	line_count++;
}

char *write_buffer = nullptr;
void write_line(const std::string &name, const std::string &text, unsigned int line_number, bool dead) {
	write_buffer = static_cast<char *>(realloc(write_buffer, ws.ws_col));
	if(!write_buffer) {
		fprintf(stderr, "Memory allocation failed\n");
		exit(1);
	}
	snprintf(write_buffer, ws.ws_col, "%c %s\033[%dG%s", dead ? 'X' : ' ', name.c_str(), longest_name + 4, text.c_str());
	fprintf(stdout, "\033[u\033[%dF\033[2K%s\033[u", line_count - line_number, write_buffer);
}

char buffer[READ_BUFFER_SIZE];
void read_input_fd(unsigned int fd_offset, bool &continue_flag) {
	fdinfo &info = fdinfos[fd_offset];
	ssize_t bread = read(info.fd, buffer, READ_BUFFER_SIZE);
	if(bread < 0) {
		if(errno == EINTR) {
			return;
		}
		fprintf(stderr, "Failed to read from %s: %m\n", info.name.c_str());
		exit(1);
	}

	if(bread == 0) {
		if(!info.current_line.empty())
			info.current_full_line = std::move(info.current_line);
		write_line(info.name, info.current_full_line, info.line_number, true);
		delpollfd(fd_offset);
		continue_flag = false;
		return;
	}

	// scan for last line
	bool found_newline = false;
	bool found_char = false;
	ssize_t start_point = 0;
	ssize_t end_point = bread;
	for(ssize_t i = bread - 1; i >= 0; i--) {
		char c = buffer[i];
		if(c == 0xA || c == 0xD) {
			found_newline = true;
			if(found_char) {
				start_point = i + 1;
				break;
			}
			else {
				end_point = i;
			}
		}
		found_char = true;
	}

	if(found_newline) {
		if(!info.current_line.empty())
			info.current_full_line = std::move(info.current_line);
		info.current_line.clear();
	}
	info.current_line.append(buffer + start_point, end_point - start_point);

	if(!info.current_line.empty())
		write_line(info.name, info.current_line, info.line_number, false);
}

void sighandler(int sig) {
	force_exit = true;
}

void sigwinchhandler(int sig) {
	resize_pending = true;
}

void showhelp(bool iserr) {
	fprintf(iserr ? stderr : stdout,
			"Usage: multipipelineview [options] [inputs ...]\n"
			"\tDisplay the most recent non-empty line from each input.\n"
			"\tEach input should be a file/FIFO or a stream-mode Unix domain socket.\n"
			"\tEach line is preceded by the file name.\n"
			"\tInputs which have closed are marked with an 'X'.\n"
			"\tLines are truncated to the terminal width.\n"
			"\tSTDOUT must be a terminal.\n"
			"Options:\n"
			"-h, --help\n"
			"\tShow this help\n"
			"-V, --version\n"
			"\tShow version information\n"
	);
	exit(iserr ? 1 : 0);
}

static struct option options[] = {
	{ "help",          no_argument,        NULL, 'h' },
	{ "version",       no_argument,        NULL, 'V' },
	{ NULL, 0, 0, 0 }
};

int main(int argc, char **argv) {
	int n = 0;
	while (n >= 0) {
		n = getopt_long(argc, argv, "hV", options, NULL);
		if (n < 0) continue;
		switch (n) {
		case 'V':
			fprintf(stdout, "%s\n\n%s\n", version_string, authors);
			exit(0);
		case '?':
		case 'h':
			showhelp(n == '?');
		}
	}

	struct sigaction new_action;
	memset(&new_action, 0, sizeof(new_action));
	new_action.sa_handler = sighandler;
	sigaction(SIGINT, &new_action, 0);
	sigaction(SIGHUP, &new_action, 0);
	sigaction(SIGTERM, &new_action, 0);
	new_action.sa_handler = sigwinchhandler;
	sigaction(SIGWINCH, &new_action, 0);
	new_action.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &new_action, 0);

	while (optind < argc) {
		open_named_input(argv[optind++]);
	}

	if(line_count == 0) {
		showhelp(true);
	}

	setbuf(stdout, nullptr);

	std::string initial(line_count, '\n');
	fprintf(stdout, "%s\033[s", initial.c_str());

	while(!force_exit && pollfds.size()) {
		if(resize_pending) {
			getwinsize();
			resize_pending = false;
			for(auto &it : fdinfos) {
				write_line(it.name, "", it.line_number, false);
			}
		}
		int n = poll(pollfds.data(), pollfds.size(), -1);
		if(n < 0) {
			if(errno == EINTR) continue;
			else break;
		}

		bool continue_flag = true;

		for(size_t i = 0; i < pollfds.size() && continue_flag; i++) {
			if(!pollfds[i].revents) continue;
			read_input_fd(i, continue_flag);
		}
	}
	return 0;
}
