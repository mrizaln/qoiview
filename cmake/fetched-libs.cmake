set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)

FetchContent_Declare(
    qoipp
    GIT_REPOSITORY https://github.com/mrizaln/qoipp
    GIT_TAG af7534fb526532c4ef7bc975ec297d0d08443487
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(qoipp)
add_library(fetch::qoipp ALIAS qoipp)
