/*
 * MISTERMIND Client
 *
 * Skladnia: client (<komputer> | <ip>) <port>
 *
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

/* === Stale === */

#define ILE_POZYCJI (5)
#define CZYTAJ_BAJTY (1)
#define PISZ_BAJTY (2)


/* === Typy === */

struct parametry {
  char *host;
  long int port;
};

/* === Zmienne === */

static const char *nazwaProgramu = "server";
static int polaczenie = -1; // desktryptor polaczenia
int proba = 0;
enum { black, darkblue, green, orange, red, silver, violet, white };

/* === Implementacja === */

static void zwolnijZasoby()
{
  if(polaczenie >= 0) {
    close(polaczenie);
  }
}

static void zakoncz(int kodWyjscia, const char *komunikat, ...) {
  va_list ap;

  if (komunikat != NULL) {
    va_start(ap, komunikat);
    vfprintf(stderr, komunikat, ap);
    va_end(ap);
  }

  if (errno != 0) {
    fprintf(stderr, ": %s", strerror(errno));
  }

  fprintf(stderr, "\n");

  zwolnijZasoby();
  exit(kodWyjscia);
}

static void czytajArgumenty(int argc, char **argv, struct parametry *param) {

  char *host, *port, *endptr;

  if (argc > 0) {
    nazwaProgramu = argv[0];
  }

  if (argc < 3) {
    zakoncz(EXIT_FAILURE, "Skladnia: %s (<komputer> | <ip>) <port>\n", nazwaProgramu);
  }

  host = argv[1];
  port = argv[2];

  errno = 0;
  param->port = strtol(port, &endptr, 10);
  if ((errno == ERANGE && (param->port == LONG_MAX || param->port == LONG_MIN))
    || (errno != 0 && param->port == 0)) {
    zakoncz(EXIT_FAILURE, "strtol");
  }

  if (param->port < 1 || param->port > 65535)
  {
    zakoncz(EXIT_FAILURE, "Uzyj poprawnego zakresu portow (1-65535)");
  }

  param->host = host;
}

int tworzPolaczenie(const char *komputer, const int port)
{
  int gniazdo, sukces = 0;
  char port_string[6];

  sprintf(port_string, "%i", port);

  struct addrinfo wejscie;
  struct addrinfo *wynik;
  memset(&wejscie, 0, sizeof(struct addrinfo));
  wejscie.ai_flags = 0;
  wejscie.ai_family = AF_INET;
  wejscie.ai_socktype = SOCK_STREAM;
  wejscie.ai_protocol = IPPROTO_TCP;
  wejscie.ai_addrlen = 0;
  wejscie.ai_addr = NULL;
  wejscie.ai_canonname = NULL;
  wejscie.ai_next = NULL;

  if (getaddrinfo(komputer, port_string, &wejscie, &wynik) < 0) {
    zakoncz(EXIT_FAILURE, "getaddrinfo");
  }

  struct addrinfo *ai_head = wynik;

  if (wynik == NULL) {
    zakoncz(EXIT_FAILURE, "Brak informacji o adresie");
  }

  do {
    gniazdo = socket(wynik->ai_family, wynik->ai_socktype, wynik->ai_protocol);
    if (gniazdo >= 0) {
      if (connect(gniazdo, wynik->ai_addr, wynik->ai_addrlen) >= 0){
        sukces = 1;
        break;
      }
    }
  } while ((wynik = wynik->ai_next) != NULL);

  freeaddrinfo(ai_head);

  if (sukces == 0) {
    zakoncz(EXIT_FAILURE, "Brak prawidlowego gniazda");
  }

  return gniazdo;
}

uint16_t pobierzKombinacje() {

  uint16_t wybraneKolory;
  char kombinacja[50];
  int dlugosc;
  int jestOK;
  int i;

  // Czytaj kombinacje uzytownika
  printf("\tWprowadz kombinacje (%d znakow z mozliwych [bdgorsvw]): ", ILE_POZYCJI);
  do {
    jestOK = 1;
    bzero(kombinacja, sizeof(kombinacja));
    fscanf(stdin, "%s", kombinacja);

    dlugosc = strlen(kombinacja);
    if (dlugosc != 5) {
      printf("\tWprowadzone znaki: %d! Powtorz: ", dlugosc);
      jestOK = 0;
      continue;
    }

    // Przetwarzaj kolory
    wybraneKolory = 0x0000;
    for (i = 0; i < strlen(kombinacja); i++) {
      uint8_t kolor;
      switch (kombinacja[i]) {
        case 'b': // 0
          kolor = black;
          break;
        case 'd': // 1
          kolor = darkblue;
          break;
        case 'g': // 2
          kolor = green;
          break;
        case 'o': // 3
          kolor = orange;
          break;
        case 'r': // 4
          kolor = red;
          break;
        case 's': // 5
          kolor = silver;
          break;
        case 'v': // 6
          kolor = violet;
          break;
        case 'w': // 7
          kolor = white;
          break;
        default:
          fprintf(stderr, "\tZly kolor '%c' w twojej kombinacji! Powtorz: ", kombinacja[i]);
          jestOK = 0;
      }
      if (jestOK == 0) {
        break;
      } else {
        wybraneKolory += (kolor << (i * 3));
      }
    }
  }
  while (jestOK == 0);

  // Ustawienie bitu parzystosci
  uint16_t parzystosc = 0;
  for (i = 0; i < 15; i++) {
    parzystosc ^= (wybraneKolory % (1 << (i+1))) >> i;
  }
  parzystosc <<= 15;

  return wybraneKolory | parzystosc;
}

void wyswietlWynik(uint8_t wynik) {
  int czerwone, biale;

  czerwone = wynik & 7;
  biale = (wynik >> 3) & 7;

  printf("\tOdpowiedz: %d czerwone / %d biale\n", czerwone, biale);
}





int main(int argc, char *argv[]) {

  struct parametry param;

  czytajArgumenty(argc, argv, &param);

  polaczenie = tworzPolaczenie(argv[1], param.port);

  uint8_t buforOdczytu;
  uint16_t nowaKombinacja;

  do {
    proba++;
    fprintf(stdout, "Runda %d:\n", proba);
    nowaKombinacja = pobierzKombinacje();
    send(polaczenie, &nowaKombinacja, PISZ_BAJTY, 0);
    DEBUG("\tWyslano 0x%x", nowaKombinacja);

    recv(polaczenie, &buforOdczytu, CZYTAJ_BAJTY, 0);
    DEBUG(" - Odebrano 0x%x\n", buforOdczytu);
    wyswietlWynik(buforOdczytu);

    // Obsluga bledow bufora
    errno = 0;
    switch (buforOdczytu >> 6) {
      case 1:
        fprintf(stderr, "Blad parzystosci\n");
        return 2;
      case 2:
        fprintf(stderr, "Przegrales\n");
        return 3;
      case 3:
        fprintf(stderr, "Blad parzystosci i przegrales, d'niestety!\n");
        return 4;
      default:
        break;
    }

    // Sprawdz czy wygrana
    if ((buforOdczytu & 7) == 5 ) {
      printf("\nWygrales w rundzie: %d\n", proba);
      return 0;
    }

  } while (1);

  return 1;
}
