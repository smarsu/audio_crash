// Copyright (c) 2020 smarsufan. All Rights Reserved.

#include <iostream>
#include <vector>
#include <string>
#include <tuple>

/// Word1 is Ground True
/// Word2 is Pred.
std::vector<std::tuple<int, int>> edit_pass(std::vector<int> word1, std::vector<int> word2) {
  size_t size1 = word1.size();
  size_t size2 = word2.size();
  
  std::vector<std::vector<int>> dis_map(size1 + 1, std::vector<int>(size2 + 1, 0));
  std::vector<std::vector<std::vector<std::tuple<int, int>>>> pass(size1 + 1, std::vector<std::vector<std::tuple<int, int>>>(size2 + 1));
  dis_map[0][0] = 0;
  pass[0][0] = {};
  for (int i = 1; i < size2 + 1; ++i) {
    dis_map[0][i] = i;
    pass[0][i] = pass[0][i - 1];
    pass[0][i].push_back(std::tuple<int, int>(-1, i - 1));  // insert
  }
  for (int j = 1; j < size1 + 1; ++j) {
    dis_map[j][0] = j;
    pass[j][0] = pass[j - 1][0];
    pass[j][0].push_back(std::tuple<int, int>(-2, j - 1));  // remove
  }
  
  for (int i = 1; i < size1 + 1; ++i) {
    for (int j = 1; j < size2 + 1; ++j) {
      int dis1 = dis_map[i - 1][j] + 1;
      int dis2 = dis_map[i][j - 1] + 1;
      int dis3 = (word1[i - 1] == word2[j - 1])
          ? dis_map[i - 1][j - 1] 
          : (dis_map[i - 1][j - 1] + 1);
      dis_map[i][j] = std::min(dis1, std::min(dis2, dis3));

      if (dis1 == dis_map[i][j]) {
        pass[i][j] = pass[i - 1][j];
        pass[i][j].push_back(std::tuple<int, int>(-2, i - 1));  // insert
      }
      else if (dis2 == dis_map[i][j]) {
        pass[i][j] = pass[i][j - 1];
        pass[i][j].push_back(std::tuple<int, int>(-1, j - 1));  // remove
      }
      else if (dis3 == dis_map[i][j]) {
        pass[i][j] = pass[i - 1][j - 1];
        if (word1[i - 1] != word2[j - 1]) {
          pass[i][j].push_back(std::tuple<int, int>(i - 1, j - 1));
        }
      }
    }
  }
  return pass[size1][size2];
}

extern "C" __attribute__((visibility("default"))) __attribute__((used))
int the_edit_pass(int *reference, int reference_size, int *hypothesis, int hypothesis_size, int *edit_pass_result) {
  const std::vector<int> reference_vec(reference, reference + reference_size);
  const std::vector<int> hypothesis_vec(hypothesis, hypothesis + hypothesis_size);
  auto pass = edit_pass(reference_vec, hypothesis_vec);
  int size = 0;
  for (auto &tuple : pass) {
    edit_pass_result[size++] = std::get<0>(tuple);
    edit_pass_result[size++] = std::get<1>(tuple);
  }
  return size;
}

#ifdef WITH_MAIN
int main() {
  std::vector<int> str1 = {1, 2, 3};
  std::vector<int> str2 = {};

  auto pass = edit_pass(str1, str2);
  for (auto &p : pass) {
    int left = std::get<0>(p);
    int right = std::get<1>(p);
    std::string op;
    if (left == -1) {
      op = "remove";
      fprintf(stderr, "%s %d\n", op.c_str(), str2[right]);
    }
    else if (left == -2) {
      op = "insert";
      fprintf(stderr, "%s %d\n", op.c_str(), str1[right]);
    }
    else {
      op = "replace";
      fprintf(stderr, "%s %d %d\n", op.c_str(), str2[right], str1[left]);
    }
  }

  return 0;
}
#endif  // WITH_MAIN
