set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)

# example:
# ~~~

FetchContent_Declare(
    qoipp
    GIT_REPOSITORY https://github.com/mrizaln/qoipp
    GIT_TAG v0.3.0
)
FetchContent_MakeAvailable(qoipp)

add_library(fetch::qoipp ALIAS qoipp)

# ~~~
