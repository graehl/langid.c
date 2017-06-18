#ifndef _LANGID_H
#define _LANGID_H

#include "langid.pb-c.h"
#include "sparseset.h"

/* Structure containing all the state required to
 * implement a language identifier
 */
typedef struct {
  unsigned int num_feats;
  unsigned int num_langs;
  unsigned int num_states;

  unsigned (*tk_nextmove)[][256];
  unsigned (*tk_output_c)[];
  unsigned (*tk_output_s)[];
  unsigned (*tk_output)[];

  double (*nb_pc)[];
  double (*nb_ptc)[];

  char* (*nb_classes)[];

  Langid__LanguageIdentifier* protobuf_model;

  /* sparsesets for counting states and features. these are
   * part of LanguageIdentifier as the clear operation on them
   * is much less costly than allocating them from scratch
   */
  Set *sv, *fv;
} LanguageIdentifier;

extern LanguageIdentifier* get_default_identifier(void);
extern LanguageIdentifier* load_identifier(char const*);
extern void destroy_identifier(LanguageIdentifier*);

typedef unsigned LangIndex;  // -1 = not found
typedef struct {
  char const* lang;
  LangIndex i;
  double logprob;
} LikelyLanguage;

extern LikelyLanguage identify_likely(LanguageIdentifier*, char const*, unsigned);
extern LikelyLanguage identify_likely_logprobs(LanguageIdentifier*, char const*, unsigned, double*);
extern LikelyLanguage likeliest(LanguageIdentifier*, double*);
extern char const* identify(LanguageIdentifier*, char const*, unsigned);
extern char const* get_lang_name(LanguageIdentifier*, LangIndex);
extern LangIndex identify_index(LanguageIdentifier*, char const*, unsigned);
extern LangIndex identify_index_logprobs(LanguageIdentifier*, char const*, unsigned, double*);
extern LangIndex get_lang_index(LanguageIdentifier*, char const*);
extern double identify_logprob(LanguageIdentifier*, LangIndex, char const*, unsigned);
extern void identify_logprobs(LanguageIdentifier*, char const*, LangIndex, double*);

/** make the largest logprob 0 and the (worse) logprobs <0 */
extern void normalize_logprobs_n(double*, LangIndex);
extern void identify_normalize_logprobs(LanguageIdentifier*, double*);
extern LangIndex logprob_to_pred_n(double*, LangIndex);
extern LangIndex logprob_to_pred(LanguageIdentifier*, double*);

#endif
