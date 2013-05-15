/*
 * MISTERMIND Server
 *
 * Skladnia: server <port> <kombinacja>
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

/* === Stale === */

#define ILOSC_PROB (10)
#define ILOSC_POZYCJI (5)
#define MAX_LICZBA_KOLOROW (8)

#define CZYTAJ_BAJTY (2)
#define PISZ_BAJTY (1)
#define BUFOR_BAJTY (2)
#define PRZESUN_BITY (3)
#define PARZYSTOSC_BIT (6)
#define PRZEGRANA_BIT (7)

#define EXIT_BLAD_PARZYSTOSCI (2)
#define EXIT_PRZEGRANA (3)
#define EXIT_PRZEGRANA_BLAD (4)

#define INFORMACJA_ZWROTNA (5)

/* === Typy === */

struct parametry {
  long int port;
  uint8_t kombinacja[ILOSC_POZYCJI];
};

/* === Zmienne === */

static const char *nazwaProgramu = "server";
volatile sig_atomic_t zwolnionoZasoby = 0;
static int gniazdo = -1; // nasluchu serwera
static int polaczenie = -1; // od klienta

/* === Implementacja === */

static uint8_t *czytajDane(int fd, uint8_t *bufor, size_t n)
{
  /* Powtarzaj dopoki nadchodza dane */
  size_t odebraneBajty = 0;
  do {
    ssize_t r;
    r = read(fd, bufor + odebraneBajty, n - odebraneBajty);
    if (r <= 0) {
      return NULL;
    }
    odebraneBajty += r;
  } while (odebraneBajty < n);

  if (odebraneBajty < n) {
    return NULL;
  }
  return bufor;
}

char *int2bin(unsigned n, char *buf)
{
    #define BITY (sizeof(n) * CHAR_BIT)

    static char bin_string[BITY + 1];
    int i;

    if (buf == NULL)
        buf = bin_string;

    for (i = BITY - 1; i >= 0; --i) {
        buf[i] = (n & 1) ? '1' : '0';
        n >>= 1;
    }

    buf[BITY] = '\0';
    return buf;

    #undef BITY
}

static int obliczOdpowiedz(uint16_t zadanie, uint8_t *odpowiedz, uint8_t *kombinacja)
{
  int pozostaleKolory[MAX_LICZBA_KOLOROW];
  int prawidloweKolory[MAX_LICZBA_KOLOROW];
  uint8_t parzystoscObliczona, parzystoscOdebrana;
  int czerwone, biale;
  int j;

  parzystoscOdebrana = (zadanie >> 15) & 1;

  // wyodrebnij prawidloweKolory i oblicz parzystosc
  parzystoscObliczona = 0;
  for (j = 0; j < ILOSC_POZYCJI; ++j) {
    int tmp = zadanie & 0x7;
    parzystoscObliczona ^= tmp ^ (tmp >> 1) ^ (tmp >> 2);
    prawidloweKolory[j] = tmp;
    zadanie >>= PRZESUN_BITY;
  }
  parzystoscObliczona &= 0x1;

  // policz czerwone i biale
  memset(&pozostaleKolory[0], 0, sizeof(pozostaleKolory));
  czerwone = biale = 0;
  for (j = 0; j < ILOSC_POZYCJI; ++j) {
    // policz czerwone
    if (prawidloweKolory[j] == kombinacja[j]) {
      czerwone++;
    } else {
      pozostaleKolory[kombinacja[j]]++;
    }
  }
  for (j = 0; j < ILOSC_POZYCJI; ++j) {
    // policz jako biale jesli nie czerwone
    if (prawidloweKolory[j] != kombinacja[j]) {
      if (pozostaleKolory[prawidloweKolory[j]] > 0) {
        biale++;
        pozostaleKolory[prawidloweKolory[j]]--;
      }
    }
  }

  // buduj odpowiedz
  odpowiedz[0] = czerwone | (biale << PRZESUN_BITY);
  if (parzystoscOdebrana != parzystoscObliczona) {
    odpowiedz[0] |= (1 << PARZYSTOSC_BIT);
    return -1;
  } else {
    return czerwone;
  }
}

static void zwolnijZasoby()
{
  // musimy zablokowac sygnaly
  sigset_t blokowaneSygnaly;
  sigfillset(&blokowaneSygnaly);
  sigprocmask(SIG_BLOCK, &blokowaneSygnaly, NULL);

  if(zwolnionoZasoby == 1) {
      return;
  }
  zwolnionoZasoby = 1;

  // zwolnij zasoby
  DEBUG("Wylaczono serwer\n");
  if(polaczenie >= 0) {
      close(polaczenie);
  }
  if(gniazdo >= 0) {
      close(gniazdo);
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

static void obsluzSygnal(int sig)
{
  DEBUG("\nOdebrano sygnal\n");
  zwolnijZasoby();
  exit(EXIT_SUCCESS);
}

static void ustawObslugeSygnalow() {

  sigset_t blokowaneSygnaly;
  int i;

  if(sigfillset(&blokowaneSygnaly) < 0) {
    zakoncz(EXIT_FAILURE, "sigfillset");
  }
  else {
    const int obsluzSygnaly[] = { SIGINT, SIGQUIT, SIGTERM };
    struct sigaction s;
    s.sa_handler = obsluzSygnal;
    memcpy(&s.sa_mask, &blokowaneSygnaly, sizeof(s.sa_mask));
    s.sa_flags = SA_RESTART;
    int iloscObslugiwanych = sizeof(obsluzSygnaly)/sizeof(obsluzSygnaly[0]);
    for(i = 0; i < iloscObslugiwanych; i++) {
      if (sigaction(obsluzSygnaly[i], &s, NULL) < 0) {
        zakoncz(EXIT_FAILURE, "sigaction");
      }
    }
  }
}

static void czytajArgumenty(int argc, char **argv, struct parametry *param) {

  char *port;
  char *kombinacja;
  char *endptr;
  int i;
  enum { black, darkblue, green, orange, red, silver, violet, white };

  if (argc > 0) {
    nazwaProgramu = argv[0];
  }

  if (argc < 3) {
    zakoncz(EXIT_FAILURE, "Skladnia: %s <port> <kombinacja>\n", nazwaProgramu);
  }

  port = argv[1];
  kombinacja = argv[2];

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

  if (strlen(kombinacja) != ILOSC_POZYCJI) {
    zakoncz(EXIT_FAILURE, "Ilosc znakow w <kombinacja> powinna wynosic: %d", ILOSC_POZYCJI);
  }

  /* czytaj kombinacje */
  for (i = 0; i < ILOSC_POZYCJI; ++i) {
    uint8_t kolor;
    switch (kombinacja[i]) {
    case 'b':
      kolor = black;
      break;
    case 'd':
      kolor = darkblue;
      break;
    case 'g':
      kolor = green;
      break;
    case 'o':
      kolor = orange;
      break;
    case 'r':
      kolor = red;
      break;
    case 's':
      kolor = silver;
      break;
    case 'v':
      kolor = violet;
      break;
    case 'w':
      kolor = white;
      break;
    default:
      zakoncz(EXIT_FAILURE, "Zly kolor '%c' w <kombinacja>\nMusi byc jeden z: " \
                        "[b]lack, [d]arkblue, [g]reen, [o]range, [r]ed, " \
                        "[s]ilver, [v]iolet, [w]hite", kombinacja[i]);
    }
    param->kombinacja[i] = kolor;
  }
}






int main(int argc, char *argv[]) {

  struct parametry param;
  int proba = 0;
  int wynik = 0;

  czytajArgumenty(argc, argv, &param);
  ustawObslugeSygnalow();

  gniazdo = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (gniazdo < 0) {
    zakoncz(EXIT_FAILURE, "socket");
  }

  struct sockaddr_in adresSerwera;
  adresSerwera.sin_family = AF_INET;
  adresSerwera.sin_port = htons(param.port);
  adresSerwera.sin_addr.s_addr = INADDR_ANY;
  bzero(&adresSerwera.sin_zero, sizeof(adresSerwera.sin_zero));

  int jeden = 1;
  if (setsockopt(gniazdo, SOL_SOCKET, SO_REUSEADDR, &jeden, sizeof(jeden)) < 0) {
    zakoncz(EXIT_FAILURE, "setsockopt SO_REUSEADDR");
  }

  if (bind(gniazdo, (struct sockaddr *) &adresSerwera, sizeof(adresSerwera)) < 0) {
    zakoncz(EXIT_FAILURE, "bind");
  }

  fprintf(stdout, "Czekam na klienta... ");
  if (listen(gniazdo, INFORMACJA_ZWROTNA) < 0) {
    zakoncz(EXIT_FAILURE, "listen");
  }

  struct sockaddr_in adresKlienta;
  socklen_t adresKlienta_length = sizeof(adresKlienta);
  polaczenie = accept(gniazdo, (struct sockaddr *) &adresKlienta, &adresKlienta_length);
  if (polaczenie < 0) {
    zakoncz(EXIT_FAILURE, "accept");
  }
  fprintf(stdout, "polaczony\n");

  wynik = EXIT_SUCCESS;
  for (proba = 1; proba <= ILOSC_PROB; ++proba) {
    uint16_t propozycja;
    static uint8_t bufor[BUFOR_BAJTY];
    int poprawne;
    int blad = 0;

    // Czytaj dane od klienta
    if (czytajDane(polaczenie, &bufor[0], CZYTAJ_BAJTY) == NULL) {
      zakoncz(EXIT_FAILURE, "czytajDane");
    }
    propozycja = (bufor[1] << 8) | bufor[0];
    DEBUG("Runda %-2d: Odebrano 0x%-5x [%s]", proba, propozycja, int2bin(propozycja, NULL));

    // Oblicz odpowiedz
    poprawne = obliczOdpowiedz(propozycja, bufor, param.kombinacja);
    if (proba == ILOSC_PROB && poprawne != ILOSC_POZYCJI) {
      bufor[0] |= 1 << PRZEGRANA_BIT;
    }

    // wyslij odpowiedz
    DEBUG(" - Wysylam 0x%-3x [%s]\n", bufor[0], int2bin(bufor[0], NULL));
    int wyslaneBajty = 0;
    int s = 0;
    do {
      s = write(polaczenie, bufor + wyslaneBajty, PISZ_BAJTY - wyslaneBajty);
      if (s < 0) {
        zakoncz(EXIT_FAILURE, "write");
      }
      wyslaneBajty += s;
    } while (wyslaneBajty < PISZ_BAJTY);

    // Sprawdz bledy i czy gra zakonczona
    if (*bufor & (1<<PARZYSTOSC_BIT)) {
      fprintf(stderr, "Blad parzystosci\n");
      blad = 1;
      wynik = EXIT_BLAD_PARZYSTOSCI;
    }
    if (*bufor & (1 << PRZEGRANA_BIT)) {
      fprintf(stderr, "Gra przegrana\n");
      blad = 1;
      if (wynik == EXIT_BLAD_PARZYSTOSCI) {
        wynik = EXIT_PRZEGRANA_BLAD;
      } else {
        wynik = EXIT_PRZEGRANA;
      }
    }
    if (blad) {
      break;
    } else if (poprawne == ILOSC_POZYCJI) {
      printf("Klient wygral w rundzie: %d\n", proba);
      break;
    }
  }

  zwolnijZasoby();
  return wynik;
}
