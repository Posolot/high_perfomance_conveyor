
class ElementOfTree:
    def __init__(self):
        self.__inputs = ""
        self.__outputs = ""
        self.__function = ""

    @property
    def inputs(self):
        return self.__inputs

    @inputs.setter
    def inputs(self, value: str):
        self.__inputs = value

    @property
    def outputs(self):
        return self.__outputs

    @outputs.setter
    def outputs(self, value: str):
        self.__outputs = value

    @property
    def function(self):
        return self.__function

    @function.setter
    def function(self, value: str):
        self.__function = value