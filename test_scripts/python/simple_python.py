
# # very simple python script.  prints every second
from time import time_ns

def wait():
    start_time = time_ns()
    while True:
        if time_ns() >= start_time + 1e9:
            break

def main():
    i = 0
    while True:
        print(i)
        i+=1
        wait()

if __name__ == "__main__":
    main()