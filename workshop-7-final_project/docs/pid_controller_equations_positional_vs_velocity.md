# PID Controller Equations: Positional vs. Velocity

Based on the analysis of Rockwell Automation's Studio 5000 PIDE Instruction, modern PLCs often implement a "velocity" algorithm rather than the traditional "positional" algorithm found in textbooks and generalized resources. Understanding the mathematical and functional differences is crucial for proper tuning and control.

## 1. The Positional Equation

The positional PID equation calculates the **actual control output** at any given moment directly from the current error, the accumulated past error (integral), and the rate of change of the error (derivative). 

### Continuous Form
$$ u(t) = K_p e(t) + K_i \int_0^t e(\tau) d\tau + K_d \frac{de(t)}{dt} $$

Where:
*   $u(t)$ = Control Output
*   $e(t)$ = Error (Setpoint - Process Variable)
*   $K_p$, $K_i$, $K_d$ = Proportional, Integral, and Derivative gains

### Discrete Form (Used in Digital Controllers)
When implemented in code, the continuous integral and derivative are approximated over discrete time steps ($\Delta t$):

$$ u_k = K_p e_k + K_i \sum_{i=0}^{k} e_i \Delta t + K_d \frac{e_k - e_{k-1}}{\Delta t} $$

**Key characteristic:** The integral term accumulates the error explicitly within the summation block. 

## 2. The Velocity Equation

Instead of calculating the absolute output position, the velocity equation calculates the **change in output** ($\Delta u_k$) from the previous execution. The current output is then determined by adding this change to the previous output.

### Discrete Velocity Form

First, calculate the change in output:
$$ \Delta u_k = K_p (e_k - e_{k-1}) + K_i e_k \Delta t + K_d \frac{e_k - 2e_{k-1} + e_{k-2}}{\Delta t} $$

Then, apply it to the previous output to get the current control signal:
$$ u_k = u_{k-1} + \Delta u_k $$

Where:
*   $\Delta u_k$ = Change in control output
*   $u_{k-1}$ = The control output from the previous time step
*   $e_k, e_{k-1}, e_{k-2}$ = Error at the current, previous, and second-previous time steps

**Key characteristic:** The integral accumulation is inherently maintained in the $u_{k-1}$ (previous output) state, rather than mathematically summing the error history.

## Advantages of the Velocity Form

The velocity form is generally preferred in advanced industrial control (like the Studio 5000 PIDE instruction) due to several operational benefits:

1.  **Bumpless Transfer & Adaptive Gains:** Because the equation only computes the *change* based on the current and immediate-past errors, you can dynamically change the $K_p$, $K_i$, and $K_d$ gains on a running process without causing a massive, instantaneous jump (a "bump") in the physical output.
2.  **Multi-Loop Integration:** It makes it significantly easier to tie multiple PIDs together (e.g., using a selector block to pick the most restrictive output from temperature and pressure loops) and feed that limitation back into the controller smoothly.
3.  **Anti-Windup:** Integral windup is inherently easier to manage because the algorithm stops integrating simply by ceasing to add $\Delta u_k$ when the final output reaches its 100% or 0% physical limits.

## A Warning on Generic Resources

When programming PLCs, generic sources like Wikipedia primarily detail the **positional** equation. Attempting to tune a velocity-based PIDE instruction using assumptions derived from the positional equation can lead to confusion and poor system response.