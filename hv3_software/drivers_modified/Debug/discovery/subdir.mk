################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../discovery/hv_discovery.c 

O_SRCS += \
../discovery/hv_discovery.o 

OBJS += \
./discovery/hv_discovery.o 

C_DEPS += \
./discovery/hv_discovery.d 


# Each subdirectory must supply rules for building sources it contributes
discovery/%.o: ../discovery/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


