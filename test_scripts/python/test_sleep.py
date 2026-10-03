
# # very simple python script.  prints every second
from time import sleep

def main():
    i = 0
    while True:
        print(i)
        i+=1
        sleep(1)

if __name__ == "__main__":
    main()