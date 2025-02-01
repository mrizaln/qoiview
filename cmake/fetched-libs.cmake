set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)

# example:
# ~~~

FetchContent_Declare(
    qoipp
    GIT_REPOSITORY https://github.com/mrizaln/qoipp
    GIT_TAG ddd6a4b6bcb9
)
FetchContent_MakeAvailable(qoipp)

add_library(fetch::qoipp ALIAS qoipp)

# ~~~
