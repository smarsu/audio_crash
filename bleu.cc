// Copyright (c) 2020 smarsufan. All Rights Reserved.

#include <vector>
#include <map>
#include <cstdint>
#include <algorithm>
#include <climits>
#include <cmath>

class Fraction {
 public:
  Fraction(int numerator, int denominator)
   : numerator(numerator), denominator(denominator) {}

  float value() {
    return static_cast<float>(numerator) / denominator;
  }

  int numerator{0};
  int denominator{0};
};

template <typename T>
int get(std::map<T, int> &count, T key, int ref = 0) {
  if (count.find(key) == count.end()) {
    // Not Found
    return ref;
  }
  return count[key];
}

std::map<int64_t, int> counter(const std::vector<int64_t> &historys) {
  std::map<int64_t, int> count;
  for (int64_t history : historys) {
    if (count.find(history) == count.end()) {
      // Not Found
      count[history] = 1;
    }
    else {
      count[history] += 1;
    }
  }
  return count;
}

int64_t make_mask(int n) {
  int64_t x = 0xffff;
  int64_t mask = 0;
  for (int idx = 0; idx < n; ++idx) {
    mask = (mask << 16) + x;
  }
  return mask;
}

std::vector<int64_t> ngrams(const std::vector<int16_t> &hypothesis, int n) {
  std::vector<int64_t> historys;
  if (hypothesis.size() < n) {
    return historys;
  }
  int64_t mask = make_mask(n);
  int64_t v = 0;
  for (int idx = 0; idx < n; ++idx) {
    v = (v << 16) + hypothesis[idx];
  }
  v &= mask;
  historys.push_back(v);
  for (int idx = n; idx < hypothesis.size(); ++idx) {
    v = (v << 16) + hypothesis[idx];
    // For 1 - 4 grams, we need mask.
    v &= mask;
    historys.push_back(v);
  }
  return historys;
}

float brevity_penalty(float closest_ref_len, float hyp_len) {
    if (hyp_len > closest_ref_len) {
      return 1;
    }
    else if (hyp_len == 0) {
      return 0;
    }
    else {
      return std::exp(1 - closest_ref_len / hyp_len);
    }
}

int closest_ref_length(const std::vector<std::vector<int16_t>> &references, int hyp_len) {
  int closest_ref_len = 0;
  int closest = INT_MAX;
  for (int idx = 0; idx < references.size(); ++idx) {
    int ref_len = references[idx].size();
    int dis = std::abs(ref_len - hyp_len);
    if (dis < closest) {
      closest = dis;
      closest_ref_len = ref_len;
    }
  }
  return closest_ref_len;
}

Fraction modified_precision(const std::vector<std::vector<int16_t>> &references, 
                            const std::vector<int16_t> &hypothesis, 
                            int n) {
  std::map<int64_t, int> counts = counter(ngrams(hypothesis, n));
  std::map<int64_t, int> max_counts;
  for (const std::vector<int16_t> &reference : references) {
    std::map<int64_t, int> reference_counts = counter(ngrams(reference, n));
    for (auto item : counts) {
      int64_t ngram = item.first;
      max_counts[ngram] = std::max(get(max_counts, ngram), get(reference_counts, ngram));
    }
  }
  std::map<int64_t, int> clipped_counts;
  for (auto item : counts) {
    int64_t ngram = item.first;
    int count = item.second;
    clipped_counts[ngram] = std::min(count, max_counts[ngram]);
  }
  int numerator = 0;
  for (auto item : clipped_counts) {
    numerator += item.second;
  }
  int denominator = 0;
  for (auto item : counts) {
    denominator += item.second;
  }
  denominator = std::max(1, denominator);
  return Fraction(numerator, denominator);
}

float bleu(const std::vector<std::vector<std::vector<int16_t>>> &list_of_references,
           const std::vector<std::vector<int16_t>> &hypotheses,
           const std::vector<float> weights = {0.25, 0.25, 0.25, 0.25},
           float epsilon = 0.1) {
  std::map<int, int> p_numerators;
  std::map<int, int> p_denominators;

  int hyp_lengths = 0;
  int ref_lengths = 0;

  if (list_of_references.size() != hypotheses.size()) {
    return -100;
  }

  for (int idx = 0; idx < list_of_references.size(); ++idx) {
    const std::vector<std::vector<int16_t>> &references = list_of_references[idx];
    const std::vector<int16_t> &hypothesis = hypotheses[idx];

    for (int ii = 0; ii < weights.size(); ++ii) {
      int n = ii + 1;
      Fraction p_i = modified_precision(references, hypothesis, n);
      p_numerators[n] = get(p_numerators, n) + p_i.numerator;
      p_denominators[n] = get(p_denominators, n) + p_i.denominator;
    }

    int hyp_len = hypothesis.size();
    hyp_lengths += hyp_len;
    ref_lengths += closest_ref_length(references, hyp_len);
  }

  float bp = brevity_penalty(ref_lengths, hyp_lengths);

  std::vector<Fraction> p_n;
  for (int idx = 0; idx < weights.size(); ++idx) {
    int n = idx + 1;
    p_n.push_back(Fraction(p_numerators[n], p_denominators[n]));
  }

  if (p_numerators[1] == 0) {
    return 0;
  }

  // Smoothen
  std::vector<float> p_n_f;
  for (int idx = 0; idx < p_n.size(); ++idx) {
    float f = 0;
    Fraction p_i = p_n[idx];
    if (p_i.numerator == 0) {
      f = (p_i.numerator + epsilon) / p_i.denominator;
    }
    else {
      f = p_i.value();
    }
    p_n_f.push_back(f);
  }

  float s = 0;
  for (int idx = 0; idx < p_n_f.size(); ++idx) {
    float w_i = weights[idx];
    float p_i = p_n_f[idx];
    s += w_i * std::log(p_i);
  }
  s = bp * std::exp(s);
  return s;
}

extern "C" __attribute__((visibility("default"))) __attribute__((used))
float sentence_bleu(int16_t *reference, int reference_size, int16_t *hypothesis, int hypothesis_size) {
  const std::vector<int16_t> reference_vec(reference, reference + reference_size);
  const std::vector<int16_t> hypothesis_vec(hypothesis, hypothesis + hypothesis_size);
  return bleu({{reference_vec}}, {hypothesis_vec});
}

#ifdef WITH_MAIN
int main() {
  std::vector<int16_t> reference = {2, 6, 6, 10, 7, 12};
  std::vector<int16_t> hypothesis = {14, 13, 4, 3, 6, 10};

  float s = bleu({{reference}}, {hypothesis});
  fprintf(stderr, "s ... %f\n", s);
}
#endif  // WITH_MAIN
