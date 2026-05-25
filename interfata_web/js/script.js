window.addEventListener("load", initpage);

function initpage() {
    const butoaneEfecte = document.querySelectorAll('.efect button');
    
    butoaneEfecte.forEach(buton => {
        buton.addEventListener('click', function() {
            let wasActive = this.classList.contains('activat');
            
            butoaneEfecte.forEach(b => b.classList.remove('activat'));
            
            let fx = "clean"; // default state
            
            if (!wasActive) {
                this.classList.add('activat');
                
                if(this.id === "distors2") fx = "dist";
                if(this.id === "reverb2") fx = "reverb";
                if(this.id === "delay2") fx = "delay";
            }

            // Using relative paths to prevent CORS issues
            fetch(`/?fx=${fx}`)
                .then(response => console.log(`Changed effect to: ${fx}`))
                .catch(err => console.log("Pico nu a răspuns: ", err));
        });
    });

    initKnobs();
}

function initKnobs() {
    const knobs = document.querySelectorAll('.knob-container');
    
    knobs.forEach(container => {
        const knob = container.querySelector('.knob');
        const ticksContainer = container.querySelector('.ticks');
        const hiddenInput = container.querySelector('input[type="hidden"]');
        const valueDisplay = container.querySelector('.knob-value'); 

        const minAngle = -135; 
        const maxAngle = 135;  
        const numTicks = 27;   
        const angleRange = maxAngle - minAngle;

        const ticks = [];
        for(let i = 0; i < numTicks; i++) {
            let tick = document.createElement('div');
            tick.className = 'tick';
            let angle = minAngle + (i / (numTicks - 1)) * angleRange;
            tick.style.transform = `rotate(${angle}deg) translateY(-65px)`; 
            ticksContainer.appendChild(tick);
            ticks.push({ el: tick, angle: angle });
        }

        let isDragging = false;
        let startY = 0;
        let startPercentage = 0;
        let currentPercentage = 0; 
        let lastSentValue = -1; // Tracks the last integer sent to the Pico

        function updateKnob(percentage) {
            percentage = Math.max(0, Math.min(1, percentage)); 
            currentPercentage = percentage; 
            
            let currentAngle = minAngle + (percentage * angleRange);
            knob.style.transform = `rotate(${currentAngle}deg)`;

            ticks.forEach(t => {
                if (t.angle <= currentAngle) {
                    t.el.classList.add('active');
                } else {
                    t.el.classList.remove('active');
                }
            });

            let numericValue = Math.round(percentage * 10);

            if(hiddenInput) {
                hiddenInput.value = numericValue;
            }

            if(valueDisplay) {
                valueDisplay.textContent = numericValue;
            }

            // Only fire a network request if the distinct integer changes
            if (hiddenInput && numericValue !== lastSentValue) {
                lastSentValue = numericValue;
                
                fetch(`/?param=${hiddenInput.id}&val=${numericValue}`)
                    .then(() => console.log(`Sent: ${hiddenInput.id} = ${numericValue}`))
                    .catch(err => console.log("Error syncing parameter:", err));
            }
        }

        const startDrag = (e) => { 
            isDragging = true; 
            startY = e.clientY || (e.touches && e.touches[0].clientY);
            startPercentage = currentPercentage; 
        };
        
        const stopDrag = () => { 
            isDragging = false; 
        };
        
        const onDrag = (e) => {
            if(!isDragging) return;
            let clientY = e.clientY || (e.touches && e.touches[0].clientY);
            if(clientY === undefined) return;

            const deltaY = clientY - startY;
            const dragSensitivity = 200; 
            const percentageChange = -(deltaY / dragSensitivity);
            
            let newPercentage = startPercentage + percentageChange;
            updateKnob(newPercentage);
        };

        knob.addEventListener('mousedown', startDrag);
        knob.addEventListener('touchstart', startDrag, {passive: false});
        window.addEventListener('mouseup', stopDrag);
        window.addEventListener('touchend', stopDrag);
        window.addEventListener('mousemove', onDrag);
        window.addEventListener('touchmove', (e) => {
            if(isDragging) e.preventDefault(); 
            onDrag(e);
        }, {passive: false});

        updateKnob(0); // Default to 0 on init
    });
}