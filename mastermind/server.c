/*
 * MISTERMIND Server
 *
 * Usage: server <port> <secret>
 *
 * Colors to use in secret (first letters):
 * [b]lack, [d]arkblue, [g]reen, [o]range, [r]ed, [s]ilver, [v]iolet, [w]hite
 *
 * Copyright (c) 2013 piniu
 */

#define ENDEBUG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <stdarg.h>
#include <netinet/in.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>

/* === Macros === */

#ifdef ENDEBUG
#define DEBUG(...) do { fprintf(stderr, __VA_ARGS__); } while(0)
#else
#define DEBUG(...)
#endif

/* === Constants === */

#define MAX_TRIES (10)
#define SLOTS (5)
#define COLORS (8)

#define READ_BYTES (2)
#define WRITE_BYTES (1)
#define BUFFER_BYTES (2)
#define SHIFT_WIDTH (3)
#define PARITY_ERR_BIT (6)
#define GAME_LOST_ERR_BIT (7)

#define EXIT_PARITY_ERROR (2)
#define EXIT_GAME_LOST (3)
#define EXIT_MULTIPLE_ERRORS (4)

#define BACKLOG (5)

/* === Type Definitions === */

struct opts {
  long int port;
  uint8_t secret[SLOTS];
};

/* === Global Variables === */

static const char *progname = "server";
volatile sig_atomic_t terminated = 0; // cleanup performed
static int sockfd = -1; // server socket file descriptor
static int connfd = -1; // connection socket file descriptor

/* === Implementations === */

static uint8_t *read_from_client(int fd, uint8_t *buffer, size_t n)
{
  /* loop, as packet can arrive in several partial reads */
  size_t bytes_recv = 0;
  do {
    ssize_t r;
    r = read(fd, buffer + bytes_recv, n - bytes_recv);
    if (r <= 0) {
      return NULL;
    }
    bytes_recv += r;
  } while (bytes_recv < n);

  if (bytes_recv < n) {
    return NULL;
  }
  return buffer;
}

char *int2bin(unsigned n, char *buf)
{
    #define BITS (sizeof(n) * CHAR_BIT)

    static char static_buf[BITS + 1];
    int i;

    if (buf == NULL)
        buf = static_buf;

    for (i = BITS - 1; i >= 0; --i) {
        buf[i] = (n & 1) ? '1' : '0';
        n >>= 1;
    }

    buf[BITS] = '\0';
    return buf;

    #undef BITS
}

static int compute_answer(uint16_t req, uint8_t *resp, uint8_t *secret)
{
  int colors_left[COLORS];
  int guess[COLORS];
  uint8_t parity_calc, parity_recv;
  int red, white;
  int j;

  parity_recv = (req >> 15) & 1;

  // extract the guess and calculate parity
  parity_calc = 0;
  for (j = 0; j < SLOTS; ++j) {
    int tmp = req & 0x7;
    parity_calc ^= tmp ^ (tmp >> 1) ^ (tmp >> 2);
    guess[j] = tmp;
    req >>= SHIFT_WIDTH;
  }
  parity_calc &= 0x1;

  // marking red and white
  memset(&colors_left[0], 0, sizeof(colors_left));
  red = white = 0;
  for (j = 0; j < SLOTS; ++j) {
    // mark red
    if (guess[j] == secret[j]) {
      red++;
    } else {
      colors_left[secret[j]]++;
    }
  }
  for (j = 0; j < SLOTS; ++j) {
    // mark white for not marked red
    if (guess[j] != secret[j]) {
      if (colors_left[guess[j]] > 0) {
        white++;
        colors_left[guess[j]]--;
      }
    }
  }

  // build response
  resp[0] = red | (white << SHIFT_WIDTH);
  if (parity_recv != parity_calc) {
    resp[0] |= (1 << PARITY_ERR_BIT);
    return -1;
  } else {
    return red;
  }
}

static void terminate()
{
  // signals need to be blocked here to avoid race
  sigset_t blocked_signals;
  sigfillset(&blocked_signals);
  sigprocmask(SIG_BLOCK, &blocked_signals, NULL);

  if(terminated == 1) {
      return;
  }
  terminated = 1;

  // clean up resources
  DEBUG("Shutting down server\n");
  if(connfd >= 0) {
      close(connfd);
  }
  if(sockfd >= 0) {
      close(sockfd);
  }
}

static void bye(int eval, const char *msg, ...) {

  va_list ap;

  if (msg != NULL) {
    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);
  }

  if (errno != 0) {
    fprintf(stderr, ": %s", strerror(errno));
  }

  fprintf(stderr, "\n");

  terminate();
  exit(eval);
}

static void signal_handler(int sig)
{
  DEBUG("\nCaught Signal\n");
  terminate();
  exit(EXIT_SUCCESS);
}

static void set_signal_handlers() {

  sigset_t block_signals;
  int i;

  if(sigfillset(&block_signals) < 0) {
    bye(EXIT_FAILURE, "sigfillset");
  }
  else {
    const int signals[] = { SIGINT, SIGQUIT, SIGTERM }; // Only these are valid
    struct sigaction s;
    s.sa_handler = signal_handler;
    memcpy(&s.sa_mask, &block_signals, sizeof(s.sa_mask));
    s.sa_flags = SA_RESTART;
    int signals_count = sizeof(signals)/sizeof(signals[0]);
    for(i = 0; i < signals_count; i++) {
      if (sigaction(signals[i], &s, NULL) < 0) {
        bye(EXIT_FAILURE, "sigaction");
      }
    }
  }
}

static void parse_args(int argc, char **argv, struct opts *options) {

  char *port;
  char *secret;
  char *endptr;
  int i;
  enum { black, darkblue, green, orange, red, silver, violet, white };

  if (argc > 0) {
    progname = argv[0];
  }

  if (argc < 3) {
    bye(EXIT_FAILURE, "Usage: %s <port> <sequence>\n", progname);
  }

  port = argv[1];
  secret = argv[2];

  errno = 0;
  options->port = strtol(port, &endptr, 10);
  if ((errno == ERANGE && (options->port == LONG_MAX || options->port == LONG_MIN))
    || (errno != 0 && options->port == 0)) {
    bye(EXIT_FAILURE, "strtol");
  }

  if (endptr == port) {
    bye(EXIT_FAILURE, "No digits were found in <port>");
  }

  if (*endptr != '\0') {
    bye(EXIT_FAILURE, "Further characters after <port>: %s", endptr);
  }

  if (options->port < 1 || options->port > 65535)
  {
    bye(EXIT_FAILURE, "Use a valid TCP/IP port range (1-65535)");
  }

  if (strlen(secret) != SLOTS) {
    bye(EXIT_FAILURE, "<secret> has to be %d chars long", SLOTS);
  }

  /* read secret */
  for (i = 0; i < SLOTS; ++i) {
    uint8_t color;
    switch (secret[i]) {
    case 'b':
      color = black;
      break;
    case 'd':
      color = darkblue;
      break;
    case 'g':
      color = green;
      break;
    case 'o':
      color = orange;
      break;
    case 'r':
      color = red;
      break;
    case 's':
      color = silver;
      break;
    case 'v':
      color = violet;
      break;
    case 'w':
      color = white;
      break;
    default:
      bye(EXIT_FAILURE, "Bad Color '%c' in <secret>\nMust be one of: " \
                        "[b]lack, [d]arkblue, [g]reen, [o]range, [r]ed, " \
                        "[s]ilver, [v]iolet, [w]hite", secret[i]);
    }
    options->secret[i] = color;
  }
}

/** main function */

int main(int argc, char *argv[]) {

  struct opts options;
  int attempt = 0;
  int ret = 0;

  parse_args(argc, argv, &options);
  set_signal_handlers();

  sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sockfd < 0) {
    bye(EXIT_FAILURE, "socket");
  }

  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(options.port);
  server_address.sin_addr.s_addr = INADDR_ANY;
  bzero(&server_address.sin_zero, sizeof(server_address.sin_zero));

  int one = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
    bye(EXIT_FAILURE, "setsockopt SO_REUSEADDR");
  }

  if (bind(sockfd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
    bye(EXIT_FAILURE, "bind");
  }

  printf("Waiting for client... ");
  if (listen(sockfd, BACKLOG) < 0) {
    bye(EXIT_FAILURE, "listen");
  }

  struct sockaddr_in client_address;
  socklen_t client_address_length = sizeof(client_address);
  connfd = accept(sockfd, (struct sockaddr *) &client_address, &client_address_length);
  if (connfd < 0) {
    bye(EXIT_FAILURE, "Error on accept");
  }
  fprintf(stdout, "connected\n");

  ret = EXIT_SUCCESS;
  for (attempt = 1; attempt <= MAX_TRIES; ++attempt) {
    uint16_t request;
    static uint8_t buffer[BUFFER_BYTES];
    int correct;
    int error = 0;

    // Read data from client
    if (read_from_client(connfd, &buffer[0], READ_BYTES) == NULL) {
      bye(EXIT_FAILURE, "read_from_client");
    }
    request = (buffer[1] << 8) | buffer[0];
    DEBUG("Round %-2d: Received 0x%-5x [%s]", attempt, request, int2bin(request, NULL));

    // Compute the answer
    correct = compute_answer(request, buffer, options.secret);
    if (attempt == MAX_TRIES && correct != SLOTS) {
      buffer[0] |= 1 << GAME_LOST_ERR_BIT;
    }

    // Send answer to client
    DEBUG(" - Sending 0x%-3x [%s]\n", buffer[0], int2bin(buffer[0], NULL));
    int bytes_sent = 0;
    int s = 0;
    do {
      s = write(connfd, buffer + bytes_sent, WRITE_BYTES - bytes_sent);
      if (s < 0) {
        bye(EXIT_FAILURE, "send");
      }
      bytes_sent += s;
    } while (bytes_sent < WRITE_BYTES);

    // Check errors and if game is over
    if (*buffer & (1<<PARITY_ERR_BIT)) {
      fprintf(stderr, "Parity error\n");
      error = 1;
      ret = EXIT_PARITY_ERROR;
    }
    if (*buffer & (1 << GAME_LOST_ERR_BIT)) {
      fprintf(stderr, "Game lost\n");
      error = 1;
      if (ret == EXIT_PARITY_ERROR) {
        ret = EXIT_MULTIPLE_ERRORS;
      } else {
        ret = EXIT_GAME_LOST;
      }
    }
    if (error) {
      break;
    } else if (correct == SLOTS) {
      printf("Client WIN (rounds: %d)\n", attempt);
      break;
    }
  }

  terminate();
  return ret;
}
