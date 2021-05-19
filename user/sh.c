#include <inc/lib.h>

int debug = 0;

struct Tokenizer {
    int   c;
    int   nc;
	char *np1;
    char *np2;
};


static char *PATH = NULL;

// gettoken(s, 0) prepares gettoken for subsequent calls and returns 0.
// gettoken(0, token) parses a shell token from the previously set string,
// null-terminates that token, stores the token pointer in '*token',
// and returns a token ID (0, '<', '>', '|', or 'w').
// Subsequent calls to 'gettoken(0, token)' will return subsequent
// tokens from the string.
int gettoken(struct Tokenizer *tkr, char *s, char **token);

// appends the current PATH to the given path if relevant
// returns 0 on success, or -E_BAD_PATH if the new path is too big
static int prepend_path(const char *path, char *path_buff) {
    if (*path == '/' || *path == '.') {
        // this is an absolute path,
        // or a path relative to the current working directory
        // use as is
        if (strlen(path) >= MAXPATHLEN) {
            return -E_BAD_PATH;
        }
        strcpy(path_buff, path);
        return 0;
    }
    char *bin_path = PATH == NULL ? "/" : PATH;
    if (strlen(path) + strlen(bin_path) + 1 >= MAXPATHLEN) {
        return -E_BAD_PATH;
    }
    strcpy(path_buff, bin_path);
    if (strcmp(bin_path, "/") != 0) {
        strcat(path_buff, "/");
    }
    strcat(path_buff, path);
    return 0;
}

static void export(char *key, char *value) {
    if (strcmp(key, "PATH") != 0) {
        cprintf("exporting variables other than PATH is not supported\n");
        return;
    }
    if (strlen(value) > PGSIZE) {
        cprintf("value to big\n");
        return;
    }
    if (PATH == NULL) {
        PATH = malloc();
        if (PATH == NULL) {
            cprintf("unable to set PATH: %e\n", -E_NO_MEM);
            return;
        }
    }
    strcpy(PATH, value);
}

static bool try_builtin(char *string) {
    struct Tokenizer tkr = {};
    gettoken(&tkr, string, 0);
    char *token;
    char c = gettoken(&tkr, 0, &token);
    if (c == 'w' && strcmp(token, "export") == 0) {
        c = gettoken(&tkr, 0, &token);
        if (c != 'w') {
            cprintf("expected key-value pair after export\n");
            return true;
        }

        char *value = strfind(token, '=');
        if (*value == '\0') {
            cprintf("export arg must contain = to export a value\n");
            return true;
        }
        // seperate key and value pair
        *value = '\0';
        value += 1;
        char *key = token;
        if (debug) {
            cprintf("exporting key \"%s\" as value \"%s\"\n", key, value);
            return true;
        }
        export(key, value);
        return true;
    } else {
        return false;
    }
}

// Parse a shell command from string 's' and execute it.
// Do not return until the shell command is finished.
// runcmd() is called in a forked child,
// so it's OK to manipulate file descriptor state.
#define MAXARGS 16
void
runcmd(char* s)
{
    struct Tokenizer tkr = {};
	char *argv[MAXARGS], *t;
	int argc, c, i, r, p[2], fd, pipe_child;

	pipe_child = 0;
	gettoken(&tkr, s, 0);

again:
	argc = 0;
	while (1) {
		switch ((c = gettoken(&tkr, 0, &t))) {

		case 'w':	// Add an argument
			if (argc == MAXARGS) {
				cprintf("too many arguments\n");
				exit();
			}
			argv[argc++] = t;
			break;

		case '<':	// Input redirection
			// Grab the filename from the argument list
			if (gettoken(&tkr, 0, &t) != 'w') {
				cprintf("syntax error: < not followed by word\n");
				exit();
			}
			// Open 't' for reading as file descriptor 0
			// (which environments use as standard input).
			// We can't open a file onto a particular descriptor,
			// so open the file as 'fd',
			// then check whether 'fd' is 0.
			// If not, dup 'fd' onto file descriptor 0,
			// then close the original 'fd'.

			// LAB 5: Your code here.
			int r;
			int fd = open(t, O_RDONLY);
			if (fd < 0){
				cprintf("%s is not a legal path\n", t);
                exit();
			}
			if (fd != 0){
				if ((r = dup(fd, 0)) < 0){
				cprintf("dup error in '<' : %d\n", r);
                exit();
				}
				if ((r = close(fd)) < 0){
				cprintf("close error in '<' : %d\n", r);
                exit();
				}
			}
			break;

		case '>':	// Output redirection
			// Grab the filename from the argument list
			if (gettoken(&tkr, 0, &t) != 'w') {
				cprintf("syntax error: > not followed by word\n");
				exit();
			}
			if ((fd = open(t, O_WRONLY|O_CREAT|O_TRUNC)) < 0) {
				cprintf("open %s for write: %e", t, fd);
				exit();
			}
			if (fd != 1) {
				dup(fd, 1);
				close(fd);
			}
			break;

		case '|':	// Pipe
			if ((r = pipe(p)) < 0) {
				cprintf("pipe: %e", r);
				exit();
			}
			if (debug)
				cprintf("PIPE: %d %d\n", p[0], p[1]);
			if ((r = fork()) < 0) {
				cprintf("fork: %e", r);
				exit();
			}
			if (r == 0) {
				if (p[0] != 0) {
					dup(p[0], 0);
					close(p[0]);
				}
				close(p[1]);
				goto again;
			} else {
				pipe_child = r;
				if (p[1] != 1) {
					dup(p[1], 1);
					close(p[1]);
				}
				close(p[0]);
				goto runit;
			}
			panic("| not implemented");
			break;

		case 0:		// String is complete
			// Run the current command!
			goto runit;

		default:
			panic("bad return %d from gettoken", c);
			break;

		}
	}

runit:
	// Return immediately if command line was empty.
	if(argc == 0) {
		if (debug)
			cprintf("EMPTY COMMAND\n");
		return;
	}

	// Clean up command line.
	// Read all commands from the filesystem
	// add PATH to the command name.
    char *cmd_name = malloc();
    if (cmd_name == NULL) {
        cprintf("spawn %s: %e\n", argv[0], -E_NO_MEM);
        exit();
    }
    r = prepend_path(argv[0], cmd_name);
    if (r<0) {
        cprintf("spawn %s: %e\n", argv[0], r);
        exit();
    }
	argv[argc] = 0;

	// Print the command.
	if (debug) {
		cprintf("[%08x] SPAWN:", thisenv->env_id);
		for (i = 0; argv[i]; i++)
			cprintf(" %s", argv[i]);
		cprintf("\n");
	}

	// Spawn the command!
	if ((r = spawn(cmd_name, (const char**) argv)) < 0)
		cprintf("spawn %s: %e\n", cmd_name, r);

	// In the parent, close all file descriptors and wait for the
	// spawned command to exit.
	close_all();
	if (r >= 0) {
		if (debug)
			cprintf("[%08x] WAIT %s %08x\n", thisenv->env_id, cmd_name, r);
		wait(r);
		if (debug)
			cprintf("[%08x] wait finished\n", thisenv->env_id);
	}

    free(cmd_name);

	// If we were the left-hand part of a pipe,
	// wait for the right-hand part to finish.
	if (pipe_child) {
		if (debug)
			cprintf("[%08x] WAIT pipe_child %08x\n", thisenv->env_id, pipe_child);
		wait(pipe_child);
		if (debug)
			cprintf("[%08x] wait finished\n", thisenv->env_id);
	}

	// Done!
	exit();
}


// Get the next token from string s.
// Set *p1 to the beginning of the token and *p2 just past the token.
// Returns
//	0 for end-of-string;
//	< for <;
//	> for >;
//	| for |;
//	w for a word.
//
// Eventually (once we parse the space where the \0 will go),
// words get nul-terminated.
#define WHITESPACE " \t\r\n"
#define SYMBOLS "<|>&;()"

int
_gettoken(char *s, char **p1, char **p2)
{
	int t;

	if (s == 0) {
		if (debug > 1)
			cprintf("GETTOKEN NULL\n");
		return 0;
	}

	if (debug > 1)
		cprintf("GETTOKEN: %s\n", s);

	*p1 = 0;
	*p2 = 0;

	while (strchr(WHITESPACE, *s))
		*s++ = 0;
	if (*s == 0) {
		if (debug > 1)
			cprintf("EOL\n");
		return 0;
	}
	if (strchr(SYMBOLS, *s)) {
		t = *s;
		*p1 = s;
		*s++ = 0;
		*p2 = s;
		if (debug > 1)
			cprintf("TOK %c\n", t);
		return t;
	}
	*p1 = s;
	while (*s && !strchr(WHITESPACE SYMBOLS, *s))
		s++;
	*p2 = s;
	if (debug > 1) {
		t = **p2;
		**p2 = 0;
		cprintf("WORD: %s\n", *p1);
		**p2 = t;
	}
	return 'w';
}

int
gettoken(struct Tokenizer *tkr, char *s, char **p1)
{
	if (s) {
		tkr->nc = _gettoken(s, &tkr->np1, &tkr->np2);
		return 0;
	}
	tkr->c = tkr->nc;
	*p1 = tkr->np1;
	tkr->nc = _gettoken(tkr->np2, &tkr->np1, &tkr->np2);
	return tkr->c;
}


void
usage(void)
{
	cprintf("usage: sh [-dix] [command-file]\n");
	exit();
}

void
umain(int argc, char **argv)
{
	int r, interactive, echocmds;
	struct Argstate args;

	interactive = '?';
	echocmds = 0;
	argstart(&argc, argv, &args);
	while ((r = argnext(&args)) >= 0)
		switch (r) {
		case 'd':
			debug++;
			break;
		case 'i':
			interactive = 1;
			break;
		case 'x':
			echocmds = 1;
			break;
		default:
			usage();
		}

	if (argc > 2)
		usage();
	if (argc == 2) {
		close(0);
		if ((r = open(argv[1], O_RDONLY)) < 0)
			panic("open %s: %e", argv[1], r);
		assert(r == 0);
	}
	if (interactive == '?')
		interactive = iscons(0);

	while (1) {
		char *buf;

		buf = readline(interactive ? "$ " : NULL);
		if (buf == NULL) {
			if (debug)
				cprintf("EXITING\n");
			exit();	// end of file
		}
		if (debug)
			cprintf("LINE: %s\n", buf);
		if (buf[0] == '#')
			continue;
		if (echocmds)
			printf("# %s\n", buf);
		if (debug)
			cprintf("BEFORE FORK\n");
		if ((r = fork()) < 0)
			panic("fork: %e", r);
		if (debug)
			cprintf("FORK: %d\n", r);
		if (r == 0) {
			runcmd(buf);
			exit();
		} else
			wait(r);
	}
}

