#include <stdio.h>
#include <unistd.h>
#include <glib.h>
#include <math.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_rng.h>
#include "foreach.h"
#include "procinfo.h"
#include "svec.h"
#include "rng.h"
const gsl_rng_type *rng_T;
gsl_rng *rng_R = NULL;

#define NTOK 2		      /* number of tokens per input line */
#define PHI0 50.0	      /* learning rate parameter */
#define NU0 0.2		      /* learning rate parameter */
int RESTART = 1;	      /* how many times to restart */
int NITER = 20;		      /* how many times to go over the data */
int NDIM = 25;		      /* dimensionality of the embedding */
double Z = 0.166;	      /* partition function approximation */
int CALCZ = 0;		      /* whether to calculate real Z */
int VMERGE = 0;		      /* whether to merge vectors at output */

typedef GQuark Tuple[NTOK];

GArray *data;
guint *update_cnt[NTOK];
guint *cnt[NTOK];
#define frq(i,j) ((double)cnt[i][j]/data->len)
svec *vec[NTOK];
svec *best_vec[NTOK];
GQuark qmax;

int main(int argc, char **argv);
void init_rng();
void free_rng();
void init_data();
void randomize_vectors();
void copy_best_vec();
void free_data();
float update_tuple(Tuple t);
float update_svec(svec x, svec y, svec y2, float xy2, float nx);
double logL();
double calcZ();

int main(int argc, char **argv) {
  g_message_init();
  g_message("hello");

  int opt;
  while((opt = getopt(argc, argv, "r:i:d:z:c2")) != -1) {
    switch(opt) {
    case 'r': RESTART = atoi(optarg); break;
    case 'i': NITER = atoi(optarg); break;
    case 'd': NDIM = atoi(optarg); break;
    case 'z': Z = atof(optarg); break;
    case 'c': CALCZ = 1; break;
    case '2': VMERGE = 1; break;
    default: g_error("Usage: scode [options] < input");
    }
  }

  init_rng();
  g_message("Reading data");
  init_data();
  g_message("Read %d tuples %d uniq tokens", data->len, qmax);

  double best_logL = 0;
  for (int start = 0; start < RESTART; start++) {
    randomize_vectors();
    double ll = logL();
    g_message("Restart %d/%d logL0=%g best=%g", 1+start, RESTART, ll, best_logL);
    if (CALCZ) g_message("Z=%g (approx %g)", calcZ(), Z);
    for (int iter = 0; iter < NITER; iter++) {
      for (int di = 0; di < data->len; di++) {
	update_tuple(g_array_index(data, Tuple, di));
      }
      ll = logL();
      g_message("Iteration %d/%d logL=%g", 1+iter, NITER, ll);
    }
    if (start == 0 || ll > best_logL) {
      g_message("Updating best_vec with logL=%g", ll);
      best_logL = ll;
      copy_best_vec();
    }
    g_message("Restart %d/%d logL1=%g best=%g", 1+start, RESTART, ll, best_logL);
    if (CALCZ) g_message("Z=%g (approx %g)", calcZ(), Z);
  }

  int nz = 0;
  for (guint q = 1; q <= qmax; q++) {
    if (best_vec[0][q] != NULL) nz++;
  }
  printf("%d\t%d\n", nz, VMERGE ? NTOK * NDIM : NDIM);
  for (guint q = 1; q <= qmax; q++) {
    if (best_vec[0][q] == NULL) continue;
    printf("%s\t%d\t", g_quark_to_string(q), cnt[0][q]);
    svec_print(best_vec[0][q]);
    if (VMERGE) {
      for (guint t = 1; t < NTOK; t++) {
	g_assert(best_vec[t][q] != NULL);
	printf("\t");
	svec_print(best_vec[t][q]);
      }
    }
    printf("\n");
  }
  fflush(stdout);
  free_data();
  free_rng();
  g_message("bye");
}

double logL() {
  double l = 0;
  for (int i = 0; i < data->len; i++) {
    GQuark *t = g_array_index(data, Tuple, i);
    GQuark x = t[0];
    GQuark y = t[1];
    float px = frq(0, x);
    float py = frq(1, y);
    svec vx = vec[0][x];
    svec vy = vec[1][y];
    float xy = svec_sqdist(vx, vy);
    l += log(px * py) - xy;
  }
  return (l / data->len - log(Z));
}

double calcZ() {
  double z = 0;
  for (guint x = 1; x <= qmax; x++) {
    if (x % 1000 == 0) fprintf(stderr, ".");
    if (cnt[0][x] == 0) continue;
    float px = frq(0, x);
    svec vx = vec[0][x];
    for (guint y = 1; y <= qmax; y++) {
      if (cnt[1][y] == 0) continue;
      float py = frq(1, y);
      svec vy = vec[1][y];
      float xy = svec_sqdist(vx, vy);
      z += px * py * exp(-xy);
    }
  }
  fprintf(stderr, "\n");
  return z;
}

float update_tuple(Tuple t) {
  GQuark x1 = t[0];
  GQuark y1 = t[1];
  guint cx = update_cnt[0][x1]++;
  guint cy = update_cnt[1][y1]++;
  float nx = NU0 * (PHI0 / (PHI0 + cx));
  float ny = NU0 * (PHI0 / (PHI0 + cy));
  svec vx1 = vec[0][x1];
  svec vy1 = vec[1][y1];
  guint rx = gsl_rng_uniform_int(rng_R, data->len);
  GQuark x2 = g_array_index(data, Tuple, rx)[0];
  guint ry = gsl_rng_uniform_int(rng_R, data->len);
  GQuark y2 = g_array_index(data, Tuple, ry)[1];
  svec vx2 = vec[0][x2];
  svec vy2 = vec[1][y2];
  float x1y2 = svec_sqdist(vx1, vy2);
  float y1x2 = svec_sqdist(vx2, vy1);
  float dx = update_svec(vx1, vy1, vy2, x1y2, nx);
  float dy = update_svec(vy1, vx1, vx2, y1x2, ny);
  return (dx > dy ? dx : dy);
}

float update_svec(svec x, svec y, svec y2, float xy2, float nx) {
  float sum_move2 = 0;
  float sum_x2 = 0;
  float exy2z = exp(-xy2) / Z;
  for (int i = x->size - 1; i >= 0; i--) {
    float xi = svec_get(x, i);
    float yi = svec_get(y, i);
    float y2i = svec_get(y2, i);
    float move = nx * (yi - xi + exy2z * (xi - y2i));
    xi += move;
    svec_set(x, i, xi);
    sum_move2 += move * move;
    sum_x2 += xi * xi;
  }
  svec_scale(x, 1 / sqrt(sum_x2));
  return sum_move2;
}
  
void init_data() {
  Tuple t;
  qmax = 0;
  data = g_array_new(FALSE, FALSE, sizeof(Tuple));
  foreach_line(buf, "") {
    int i = 0;
    foreach_token(tok, buf) {
      g_assert(i < NTOK);
      GQuark q = g_quark_from_string(tok);
      if (q > qmax) qmax = q;
      t[i++] = q;
    }
    g_assert(i == NTOK);
    g_array_append_val(data, t);
  }
  for (int i = 0; i < NTOK; i++) {
    update_cnt[i] = g_new0(guint, qmax+1);
    cnt[i] = g_new0(guint, qmax+1);
    vec[i] = g_new0(svec, qmax+1);
    best_vec[i] = g_new0(svec, qmax+1);
  }
  for (int i = 0; i < data->len; i++) {
    GQuark *p = g_array_index(data, Tuple, i);    
    for (int j = 0; j < NTOK; j++) {
      int k = p[j];
      g_assert(k <= qmax);
      cnt[j][k]++;
      if (vec[j][k] == NULL) {
	vec[j][k] = svec_alloc(NDIM);
	best_vec[j][k] = svec_alloc(NDIM);
      }
    }
  }
}

void randomize_vectors() {
  for (int j = 0; j < NTOK; j++) {
    for (guint q = 1; q <= qmax; q++) {
      if (vec[j][q] != NULL) {
	svec_randomize(vec[j][q]);
	update_cnt[j][q] = 0;
      }
    }
  }
}

void copy_best_vec() {
  for (int j = 0; j < NTOK; j++) {
    for (guint q = 1; q <= qmax; q++) {
      if (vec[j][q] != NULL) {
	svec_memcpy(best_vec[j][q], vec[j][q]);
      }
    }
  }
}

void free_data() {
  for (int i = 0; i < NTOK; i++) {
    for (int j = 0; j <= qmax; j++) {
      if (vec[i][j] != NULL) {
	svec_free(vec[i][j]);
	svec_free(best_vec[i][j]);
      }
    }
    g_free(vec[i]);
    g_free(best_vec[i]);
    g_free(update_cnt[i]);
  }
  g_array_free(data, TRUE);
}

void init_rng() {
  gsl_rng_env_setup();
  rng_T = gsl_rng_default;
  rng_R = gsl_rng_alloc(rng_T);
}

void free_rng() {
  gsl_rng_free(rng_R);
}

