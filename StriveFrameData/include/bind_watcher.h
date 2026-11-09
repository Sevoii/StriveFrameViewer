/*
  Utility for watching for keyboard inputs on another thread
*/
#pragma once

#include <vector>

namespace BindWatcherI {
  void addToFilter(unsigned long code);
  void clearInputs(); // drop inputs that have not been read yet
  std::vector<int> getInputs(bool await = false); // returns inputs since last call. If await: block until there is an input to return
};