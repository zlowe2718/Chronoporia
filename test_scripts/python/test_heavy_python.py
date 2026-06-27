class A:
    def __init__(self):
        self.times_called = 0

    def update(self, time_step):
        self.times_called += 1
        some = [i for i in range(1000000)]
        print(self.times_called)


class B:
    def __init__(self, time_step):
        self.time_step = time_step
        self.time_list = [time_step]
        self.long_list = []

    def update(self, time_step):
        self.long_list.extend([i for i in range(1000000)])
        self.time_step = time_step
        self.time_list.append(time_step)
        print(self.time_list)
        if time_step % 100 == 0:
            self.long_list = []


class C:
    def __init__(self, time_step, a: A):
        self.time_step = time_step
        self.a = a

    def update(self, time_step):
        self.a.update(time_step)
        self.time_step = time_step

def main():
    a = A()
    b = B(0)
    c = C(0, a)

    current_timestep = 1
    while True:
        a.update(current_timestep)
        b.update(current_timestep)
        c.update(current_timestep)
        current_timestep += 1



if __name__ == "__main__":
    main()