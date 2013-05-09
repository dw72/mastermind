/*
 * MISTERMIND Client
 *
 * Usage: client (<hostname> | <ip>) <port>
 *
 * Copyright (c) 2013 piniu
 */

#define ENDEBUG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>

/* === Macros === */

#ifdef ENDEBUG
#define DEBUG(...) do { fprintf(stderr, __VA_ARGS__); } while(0)
#else
#define DEBUG(...)
#endif

/* === Constants === */

#define SLOTS (5)
#define READ_BYTES (1)
#define WRITE_BYTES (2)


/* === Type Definitions === */

struct opts {
  char *host;
  long int port;
};

/* === Global Variables === */

static const char *progname = "server";
static int connfd = -1; // connection socket file descriptor
int attempt = 0;
enum { black, darkblue, green, orange, red, silver, violet, white };

/* === Implementations === */

static void terminate()
{
  if(connfd >= 0) {
    close(connfd);
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

static void parse_args(int argc, char **argv, struct opts *options) {

  char *host, *port, *endptr;

  if (argc > 0) {
    progname = argv[0];
  }

  if (argc < 3) {
    bye(EXIT_FAILURE, "Usage: %s (<hostname> | <ip>) <port>\n", progname);
  }

  host = argv[1];
  port = argv[2];

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

  options->host = host; // TODO: Think about some checks of hostname or ip
}

int create_connection(const char *hostname, const int port)
{
  int sock, success = 0;
  char port_string[6];

  sprintf(port_string, "%i", port);

  struct addrinfo hints;
  struct addrinfo *result;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_flags = 0;
  hints.ai_family = AF_INET; // IPv4 only
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_addrlen = 0;
  hints.ai_addr = NULL;
  hints.ai_canonname = NULL;
  hints.ai_next = NULL;

  if (getaddrinfo(hostname, port_string, &hints, &result) < 0) {
    bye(EXIT_FAILURE, "getaddrinfo");
  }

  struct addrinfo *ai_head = result;

  if (result == NULL) {
    bye(EXIT_FAILURE, "Address infromation is empty");
  }

  do {
    sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock >= 0) {
      if (connect(sock, result->ai_addr, result->ai_addrlen) >= 0){
        success = 1;
        break;
      }
    }
  } while ((result = result->ai_next) != NULL);

  freeaddrinfo(ai_head);

  if (success == 0) {
    bye(EXIT_FAILURE, "No valid socket");
  }

  return sock;
}

uint16_t get_next_offer() {

  uint16_t selected_colors;
  char offer[50];
  int len;
  int isok;
  int i;

  // Read tip from user
  printf("\tEnter your tip (%d chars from [bdgorsvw]): ", SLOTS);
  do {
    isok = 1;
    bzero(offer, sizeof(offer));
    fscanf(stdin, "%s", offer);

    len = strlen(offer);
    if (len != 5) {
      printf("\tYou entered %d chars! Repeat: ", len);
      isok = 0;
      continue;
    }

    // Process it
    selected_colors = 0x0000;
    for (i = 0; i < strlen(offer); i++) {
      uint8_t color;
      switch (offer[i]) {
        case 'b': // 0
          color = black;
          break;
        case 'd': // 1
          color = darkblue;
          break;
        case 'g': // 2
          color = green;
          break;
        case 'o': // 3
          color = orange;
          break;
        case 'r': // 4
          color = red;
          break;
        case 's': // 5
          color = silver;
          break;
        case 'v': // 6
          color = violet;
          break;
        case 'w': // 7
          color = white;
          break;
        default:
          fprintf(stderr, "\tBad Color '%c' in your offer! Repeat: ", offer[i]);
          isok = 0;
      }
      if (isok == 0) {
        break;
      } else {
        selected_colors += (color << (i * 3));
      }
    }
  }
  while (isok == 0);

  // Set parity bit
  uint16_t parity = 0;
  for (i = 0; i < 15; i++) {
    parity ^= (selected_colors % (1 << (i+1))) >> i;
  }
  parity <<= 15;

  return selected_colors | parity;
}

void display_result(uint8_t result) {
  int red, white;

  red = result & 7;
  white = (result >> 3) & 7;

  printf("\tAnswer: %d red / %d white\n", red, white);
}

/** main function */
int main(int argc, char *argv[]) {

  struct opts options;

  parse_args(argc, argv, &options);

  connfd = create_connection(argv[1], options.port);

  uint8_t read_buffer;
  uint16_t next_offer;

  do {
    attempt++;
    fprintf(stdout, "Round %d:\n", attempt);
    next_offer = get_next_offer();
    send(connfd, &next_offer, WRITE_BYTES, 0);
    DEBUG("\tSent 0x%x", next_offer);

    recv(connfd, &read_buffer, READ_BYTES, 0);
    DEBUG(" - Received 0x%x\n", read_buffer);
    display_result(read_buffer);

    // Check buffer errors
    errno = 0;
    switch (read_buffer >> 6) {
      case 1:
        fprintf(stderr, "Parity error\n");
        return 2;
      case 2:
        fprintf(stderr, "You loose\n");
        return 3;
      case 3:
        fprintf(stderr, "Parity error AND game lost, d'oh!\n");
        return 4;
      default:
        break;
    }

    // Check if game is won
    if ((read_buffer & 7) == 5 ) {
      printf("\nYou win in round: %d\n", attempt);
      return 0;
    }

  } while (1);

  return 1;
}
