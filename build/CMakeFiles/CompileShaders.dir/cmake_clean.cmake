file(REMOVE_RECURSE
  "CMakeFiles/CompileShaders"
  "shaders/ml_processing.comp.spv"
  "shaders/pattern_similarity.comp.spv"
  "shaders/topk_selection.comp.spv"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/CompileShaders.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
