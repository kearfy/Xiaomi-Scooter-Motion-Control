class Rable {
    #root = null;
    #ready = false;
    #listeners = {};
    #components = {};
    data = {};
    functions = {};

    constructor(options = {}) {
        const eventTransporter = new EventTarget;
        this.eventTransporter = eventTransporter;

        const validator = root => ({
            get: (target, key) => {
                if (typeof target[key] === 'object' && target[key] !== null) {
                    return new Proxy(target[key], validator(root ? root : key))
                } else {
                    return target[key];
                }
            },
            set: (obj, prop, value) => {
                if (typeof value == 'function') value = value.bind(this.data);
                obj[prop] = value;
                eventTransporter.dispatchEvent(new CustomEvent('triggerListeners', { detail: { listeners: 'data:updated', additionalInformation: root ? root : prop } } ));
                return true;
            }
        })

        this.data = new Proxy({}, validator(false));
        if (options.data) Object.keys(options.data).forEach(key => this.data[key] = options.data[key]);
        this.addEventListeners();
    }

    addEventListeners() {
        this.eventTransporter.addEventListener('triggerListeners', e => this.triggerListeners(e.detail.listeners, e.detail.additionalInformation));
        this.eventTransporter.addEventListener('retrieveData', e => e.detail.resolve(this.data));
        this.eventTransporter.addEventListener('retrieveScopeData', e => e.detail.resolve(this.data));
        this.eventTransporter.addEventListener('registerListener', e => {
            if (!this.#listeners[e.detail.type]) this.#listeners[e.detail.type] = [];
            this.#listeners[e.detail.type].push(e.detail.listener);
        });
    }

    triggerListeners(listener, additionalInformation) {
        if (!this.#ready) return false;
        if (this.#listeners[listener]) {
            this.#listeners[listener].forEach(listener => listener(additionalInformation));
            return true;
        } else {
            return false;
        }
    }

    async importComponent(name, path, raw = null) {
        name = name.toLowerCase();
        if (this.#components[name]) return false;
        if (!raw) raw = await (await fetch(path)).text();
        this.#components[name] = raw;
        return true;
    }

    mount(query) {
        let queried = document.querySelector(query);
        if (queried) {
            this.#root = queried;
            processElementAttributes(this.#root, this.eventTransporter, this.#components, this.data);
            processTextNodes(this.#root, this.eventTransporter);
            this.#ready = true;
            this.triggerListeners('data:updated', false);
            return true;
        } else {
            return false;
        }
    }
}

class Scheduler {
    #order;
    #validators;
    #scheduled;
    constructor(order) {
        this.#order = order;
        this.#validators = [];
        this.#scheduled = {};
    }

    registerValidator(type, validator) {
        if (this.#order.includes(type)) {
            if (!this.#validators[type]) this.#validators[type] = [];
            this.#validators[type].push(validator);
        }
    }

    readyFor(type) {
        if (!this.#order.includes(type)) return false;
        for(var i = 0; i < this.#order.length; i++) {
            const currentType = this.#order[i];
            if (type == currentType) return true;
            if (typeof this.#validators[currentType] == 'object') {
                for(var j = 0; j < this.#validators[currentType].length; j++) {
                    if (!this.#validators[currentType][j]()) {
                        return false;
                    }
                }
            }
        }

        return false;
    }

    schedule(type, runner) {
        if (!this.#order.includes(type)) return false;
        if (this.readyFor(type)) {
            runner();
        } else {
            if (!this.#scheduled[type]) this.#scheduled[type] = [];
            this.#scheduled[type].push(runner);
        }
    }

    triggerTasks() {
        Object.keys(this.#scheduled).forEach(type => {
            if (this.readyFor(type)) {
                this.#scheduled[type].forEach(runner => runner());
                this.#scheduled[type] = [];
            }
        });
    }
}

function getProperty(obj, property) {
    var current = obj;
    property.split('.').forEach(prop => current = typeof current == 'object' ? current[prop] : null);
    return current;
}

function setProperty(obj, property, value) {
    if (!property.includes('.')) obj[property] = value;
    var current = obj[property.split('.').slice(0, 1)];
    property.split('.').slice(1, -1).forEach(prop => current = typeof current == 'object' ? current[prop] : null);
    if (typeof current == 'object') {
        current[property.split('.').slice(-1)] = value;
        return true;
    } else {
        return false;
    }
}

function processTextNodes(el, eventTransporter) {
    let nodes = el.childNodes;
    nodes.forEach(node => {
        if (!node.parentNode.doNotProcessTextNodes) {
            const activeEventTransporter = (node.eventTransporter ? node.eventTransporter : eventTransporter);
            if (node.nodeName == '#text') {
                node.originalData = node.data;
                let matches = [...node.data.matchAll(/{{(.*?)}}/g)];
                if (matches.length > 0) matches.forEach(match => {
                    activeEventTransporter.dispatchEvent(new CustomEvent('registerListener', {
                        detail: {
                            type: 'data:updated',
                            listener: async () => {
                                const data = await new Promise(resolve => activeEventTransporter.dispatchEvent(new CustomEvent('retrieveData', { detail: { resolve } })));
                                const scopeData = await new Promise(resolve => activeEventTransporter.dispatchEvent(new CustomEvent('retrieveScopeData', { detail: { resolve } })));                                
                                node.data = node.originalData.replaceAll(/{{(.*?)}}/g, (match, target) => {
                                    let keys = Object.keys(data);
                                    keys.push('return ' + target);
                                    let runner = Function.apply({}, keys);
                                    try {
                                        let res = runner.apply(scopeData, Object.values(data));
                                        return res;
                                    } catch(e) {
                                        console.error(e);
                                        return undefined;
                                    }
                                });
                            }
                        }
                    }));
                });
            } else if (node.childNodes.length > 0 && !node.doNotProcessChildNodes) {
                if (node.getAttribute('rable:norender') !== null || node.getAttribute('rbl:norender') !== null || node.getAttribute(':norender') !== null || node.getAttribute('rable:no-render') !== null || node.getAttribute('rbl:no-render') !== null || node.getAttribute(':no-render') !== null) return;
                processTextNodes(node, activeEventTransporter);
            }
        }
    });
}

function processElementAttributes(el, eventTransporter, components, rawData) {
    const logic_if = [];
    var latestif = 0;

    // IF - ELSEIF - ELSE
    eventTransporter.dispatchEvent(new CustomEvent('registerListener', {
        detail: {
            type: 'data:updated',
            listener: async () => {
                for(var i = 0; i < logic_if.length; i++) {
                    var prevRes = false;
                    const logic = logic_if[i];

                    for(var j = 0; j < logic.length; j++) {
                        const block = logic[j];
                        if (!prevRes) {
                            if (block.validator) {
                                const data = await new Promise(resolve => eventTransporter.dispatchEvent(new CustomEvent('retrieveData', { detail: { resolve } })));
                                const scopeData = await new Promise(resolve => eventTransporter.dispatchEvent(new CustomEvent('retrieveScopeData', { detail: { resolve } })));
                                let keys = Object.keys(data);
                                keys.push('return ' + block.validator);
                                let runner = Function.apply({}, keys);
                                try {
                                    prevRes = runner.apply(scopeData, Object.values(data));
                                    if (prevRes) {
                                        block.node.style.display = null;
                                    } else {
                                        block.node.style.display = 'none';
                                    }
                                } catch(e) {
                                    console.error(e);
                                    block.node.style.display = 'none';
                                }
                            } else {
                                block.node.style.display = null;
                            }
                        } else {
                            block.node.style.display = 'none';
                        }
                    }
                }
            }
        }
    }));

    let nodes = el.childNodes;
    nodes.forEach(node => {
        const originalNode = node.cloneNode(true);
        if (node.nodeName != '#text' && (node.getAttribute('rable:norender') !== null || node.getAttribute('rbl:norender') !== null || node.getAttribute(':norender') !== null || node.getAttribute('rable:no-render') !== null || node.getAttribute('rbl:no-render') !== null || node.getAttribute(':no-render') !== null)) return;
        if (node.nodeName != '#text') {
            var component = null;
            if (Object.keys(components).includes(node.nodeName.toLowerCase())) {
                component = {
                    identifier: 'component-' + Math.random().toString(36).substring(2, 15) + Math.random().toString(36).substring(2, 15),
                    type: node.nodeName.toLowerCase(),
                    scheduler: new Scheduler(['dom-synced', 'bind', 'event']),
                    parsed: document.createElement('parsed'),
                    eventTransporter: new EventTarget,
                    listeners: {},
                    binders: {},
                    events: {},
                    data: {}
                };


                var dataImportFinished = false;
                const originalEventTransporter = eventTransporter;
                const validator = root => ({
                    get: (target, key) => {
                        if (!root && component.binders[key]) {
                            return getProperty(rawData, component.binders[key]);
                        } else if (typeof target[key] === 'object' && target[key] !== null) {
                            return new Proxy(target[key], validator(root ? root : key))
                        } else {
                            return target[key];
                        }
                    },
                    set: (obj, prop, value) => {
                        if (typeof value == 'function') return;
                        if (dataImportFinished && !root && component.binders[prop]) {
                            setProperty(rawData, component.binders[prop], value);
                        } else {
                            let processed = (typeof value == 'string' ? value.replaceAll(/{{(.*?)}}/g, (match, target) => {
                                switch(target) {
                                    case 'rnd':
                                    case 'random':
                                        return 'random-' + Math.random().toString(36).substring(2, 15) + Math.random().toString(36).substring(2, 15);
                                    default:
                                        return "UNKNOWN";
                                }
                            }) : value);

                            obj[prop] = processed;
                        }

                        component.eventTransporter.dispatchEvent(new CustomEvent('triggerListeners', { detail: { listeners: 'data:updated', additionalInformation: root ? root : prop } } ));
                        return true;
                    }
                });

                component.parsed.innerHTML = components[node.nodeName.toLowerCase()];
                component.data = new Proxy({}, validator(false));
                if (component.parsed.querySelector('data')) {
                    try {
                        let data = JSON.parse(component.parsed.querySelector('data').innerText);
                        Object.keys(data).forEach(key => component.data[key] = data[key]);
                        dataImportFinished = true;
                    } catch(e) {
                        console.error("Failed to parse data from component: ", e);
                    }
                }

                originalEventTransporter.dispatchEvent(new CustomEvent('registerListener', {
                    detail: {
                        type: 'data:updated',
                        listener: async item => {
                            Object.keys(component.binders).forEach(key => {
                                if (component.binders[key] == item) {
                                    component.eventTransporter.dispatchEvent(new CustomEvent('triggerListeners', { detail: { listeners: 'data:updated', additionalInformation: key } } ));
                                }
                            });
                        }
                    }
                }));
                
                component.eventTransporter.addEventListener('retrieveData', e => e.detail.resolve(component.data));                        
                component.eventTransporter.addEventListener('retrieveScopeData', e => e.detail.resolve(component.data));
                component.eventTransporter.addEventListener('triggerListeners', e => {
                    if (component.listeners[e.detail.listeners]) {
                        component.listeners[e.detail.listeners].forEach(listener => listener(e.detail.additionalInformation));
                        return true;
                    } else {
                        return false;
                    }
                });

                component.eventTransporter.addEventListener('registerListener', e => {
                    if (!component.listeners[e.detail.type]) component.listeners[e.detail.type] = [];
                    component.listeners[e.detail.type].push(e.detail.listener);
                });

                component.eventTransporter.addEventListener('triggerComponentEvent', async e => {
                    if (component.events[e.detail.event]) {
                        component.scheduler.schedule('event', async () => {
                            const data = await new Promise(resolve => originalEventTransporter.dispatchEvent(new CustomEvent('retrieveData', { detail: { resolve } })));
                            const scopeData = await new Promise(resolve => originalEventTransporter.dispatchEvent(new CustomEvent('retrieveScopeData', { detail: { resolve } })));
                            component.events[e.detail.event].forEach(runnerCode => {
                                const localData = {...data};
                                localData.event = e.detail.eventPayload;
                                localData.componentData = component.data;
                                let keys = Object.keys(localData);
                                keys.push(runnerCode);
                                let runner = Function.apply(localData, keys);
                                try {
                                    runner.apply(scopeData, Object.values(localData));
                                } catch(e) {
                                    console.error(e);
                                }
                            });
                        });
                    }
                });

                component.eventTransporter.addEventListener('registerSchedulerValidator', e => component.scheduler.registerValidator(e.detail.type, e.detail.validator));
                component.eventTransporter.addEventListener('triggerScheduledTasks', e => component.scheduler.triggerTasks());

                if (!document.head.querySelector('style[component-type=' + component.type + ']')) {
                    component.style = component.parsed.querySelector('style');
                    component.style.setAttribute('component-type', component.type);
                    component.style.innerText = component.style.innerText.replaceAll('\n', '').replaceAll(/(.*?)\{(.*?)}/gm, match => match.split('{')[0].split(',').map(target => {
                        var classes = [...target.trim().matchAll(/^\.([a-zA-Z][\w-_]+)/g)];
                        var ids = [...target.trim().matchAll(/^\#([a-zA-Z][\w-_]+)/g, '#$1[component]')];

                        if (classes.length > 0 && component.parsed.querySelector('component').children[0].classList.contains(classes[0][1])) {
                            return target.trim().replaceAll(/^\.([a-zA-Z][\w-_]+)/g, '.$1[component-type=' + component.type + ']');
                        } else if (ids.length > 0 && component.parsed.querySelector('component').children[0].id == ids[0][1]) {
                            return target.trim().replaceAll(/^\.([a-zA-Z][\w-_]+)/g, '#$1[component-type=' + component.type + ']');
                        } else {
                            return '[component-type=' + component.type + '] ' + target.trim()
                        }
                    }).join(', ') + ' {' + match.split('{')[1]);
                    document.head.appendChild(component.style);
                }

                component.replacement = component.parsed.querySelector('component').children[0];
                component.replacement.eventTransporter = component.eventTransporter;
                component.replacement.setAttribute('component-identifier', component.identifier);
                component.replacement.setAttribute('component-type', component.type);
                node.parentNode.replaceChild(component.replacement, node);
                node = component.replacement;

                [...originalNode.attributes].forEach(async attribute => {
                    var attrName = attribute.name;
                    if (attrName.slice(0, 1) == '@') attrName = ':on:' + attrName.slice(1);
                    if (attrName.slice(0, 1) == '$') attrName = ':data:' + attrName.slice(1);
                    if (attrName.slice(0, 1) == '&') attrName = ':bind:' + attrName.slice(1);
                    if (attrName.slice(0, 1) == ':' || attrName.slice(0, 4) == 'rbl:' || attrName.slice(0, 6) == 'rable:') {
                        let processedName = attrName.split(':').slice(1);
                        if (processedName[0]) switch(processedName[0]) {
                            case 'on':
                            case 'event':
                                if (processedName[1] && attribute.value !== '') {
                                    if (!component.events[processedName[1]]) component.events[processedName[1]] = [];
                                    component.events[processedName[1]].push(attribute.value);
                                }
        
                                break;
                            case 'bind':
                                if (processedName[1]) {
                                    const origin = processedName[1];
                                    const target = (attribute.value !== '' ? attribute.value : origin);
                                    component.binders[target] = origin;
                                }

                                break;
                            case 'data':
                                if (processedName[1] && attribute.value !== '') setProperty(component.data, processedName[1], attribute.value);
                                break;
                            case 'if':
                                if (logic_if[latestif]) latestif++;
                                logic_if[latestif] = [];
                                logic_if[latestif].push({
                                    node: node,
                                    validator: attribute.value
                                });
                                break;
                            case 'elseif':
                            case 'else-if':
                                if (!logic_if[latestif]) {
                                    console.error("If statement should start with if block!");
                                } else {
                                    logic_if[latestif].push({
                                        node: node,
                                        validator: attribute.value
                                    });
                                }
                                break;
                            case 'else':
                                if (!logic_if[latestif]) {
                                    console.error("If statement should start with if block!");
                                } else {
                                    logic_if[latestif].push({
                                        node: node
                                    });

                                    latestif++;
                                }
                                break;  
                        }
                    }
                });
            }

            const activeEventTransporter = (node.eventTransporter ? node.eventTransporter : eventTransporter);
            [...node.attributes].forEach(async attribute => {
                var attrName = attribute.name;
                if (attrName.slice(0, 1) == '@') attrName = ':on:' + attrName.slice(1);
                if (attrName.slice(0, 1) == ':' || attrName.slice(0, 4) == 'rbl:' || attrName.slice(0, 6) == 'rable:') {
                    let processedName = attrName.split(':').slice(1);
                    if (processedName[0]) switch(processedName[0]) {
                        case 'on':
                        case 'event':
                            if (processedName[1]) {
                                if (attribute.value !== '') {
                                    const data = await new Promise(resolve => activeEventTransporter.dispatchEvent(new CustomEvent('retrieveData', { detail: { resolve } })));
                                    const scopeData = await new Promise(resolve => activeEventTransporter.dispatchEvent(new CustomEvent('retrieveScopeData', { detail: { resolve } })));
                                    node.addEventListener(processedName[1], e => {
                                        const localData = {...data};
                                        localData.event = e;
                                        let keys = Object.keys(localData);
                                        keys.push(attribute.value);
                                        let runner = Function.apply(localData, keys);
                                        try {
                                            runner.apply(scopeData, Object.values(localData));
                                        } catch(e) {
                                            console.error(e);
                                        }
                                    });
                                } else if (!processedName[2]) {
                                    node.addEventListener(processedName[1], e => activeEventTransporter.dispatchEvent(new CustomEvent('triggerComponentEvent', {
                                        detail: {
                                            event: processedName[1],
                                            eventPayload: e
                                        }
                                    })));
                                }
                                
                                if (processedName[2]) {
                                    node.addEventListener(processedName[1], e => activeEventTransporter.dispatchEvent(new CustomEvent('triggerComponentEvent', {
                                        detail: {
                                            event: processedName[2],
                                            eventPayload: e
                                        }
                                    })));
                                }
                            }
    
                            node.removeAttribute(attribute.name);
                            break;
                        case 'for':
                            var loopStatement = attribute.value;
                            const forIdentifier = Math.random().toString(36).substring(2, 15) + Math.random().toString(36).substring(2, 15);
                            const parentNode = node.parentNode;
                            const clonedNode = node.cloneNode(true);
                            clonedNode.removeAttribute(attribute.name);

                            node.doNotProcessTextNodes = true;
                            node.doNotProcessChildNodes = true;
                            node.forIdentifier = forIdentifier;
                            node.forMasterNode = true;
                            node.style.display = 'none';
                            
                            activeEventTransporter.dispatchEvent(new CustomEvent('registerListener', {
                                detail: {
                                    type: 'data:updated',
                                    listener: async () => {
                                        const data = await new Promise(resolve => activeEventTransporter.dispatchEvent(new CustomEvent('retrieveData', { detail: { resolve } })));
                                        const asloop = (res => (res.length > 0 ? res[0].slice(1, 4) : null))([...loopStatement.matchAll(/^(.*)\sas\s(.*)\s\=\>\s(.*)$/g)]),
                                              inloop = (res => (res.length > 0 ? res[0].slice(1, 3) : null))([...loopStatement.matchAll(/^(.*)\sin\s(.*)$/g)]);

                                        if (asloop) {
                                            var target = asloop[0];
                                            var key = asloop[1];
                                            var value = asloop[2];
                                        } else if (inloop) {
                                            var target = inloop[1];
                                            var key = null;
                                            var value = inloop[0];
                                        } else {
                                            console.error(loopStatement, "is not a valid loop statement.");
                                            return;
                                        }

                                        var lastNode = null;
                                        var masterNode = null;
                                        [...parentNode.children].forEach(el => {
                                            if (el.forIdentifier == forIdentifier) {
                                                if (el.forMasterNode) {
                                                    masterNode = el;
                                                    masterNode.doNotProcessTextNodes = true;
                                                } else {
                                                    el.parentNode.removeChild(el);
                                                }
                                            }
                                        });

                                        if (typeof data[target] == 'object') {
                                            const keys = Object.keys(data[target]);
                                            const values = Object.values(data[target]);
                                            if (keys.length < 1) {
                                                masterNode.style.display = 'none';
                                            } else {
                                                for (var i = 0; i < keys.length; i++) {
                                                    const item = keys[i];
                                                    const replacementNode = clonedNode.cloneNode(true);
                                                    var updatedData = {...data};
                                                    updatedData[value] = data[target][item];
                                                    if (key) updatedData[key] = item;

                                                    const validator = root => ({
                                                        get: (obj, prop) => {
                                                            if (prop == value) {
                                                                return data[target][item];
                                                            } else if (key && prop == key) {
                                                                return item;
                                                            } else {
                                                                return data[prop];
                                                            }
                                                        },
                                                        set: (obj, prop, itemValue) => {
                                                            data[prop] = itemValue;
                                                            return true;
                                                        }
                                                    });
                                            
                                                    updatedData = new Proxy(updatedData, validator(false));

                                                    const temporaryEventTransporter = new EventTarget;
                                                    temporaryEventTransporter.addEventListener('retrieveData', e => e.detail.resolve(updatedData));   
                                                    temporaryEventTransporter.addEventListener('registerListener', e => e.detail.listener());                                     
                                                    temporaryEventTransporter.addEventListener('retrieveScopeData', e => e.detail.resolve(data));
    
                                                    processElementAttributes(replacementNode, temporaryEventTransporter, components, updatedData);
                                                    processTextNodes(replacementNode, temporaryEventTransporter);
                                                    replacementNode.forIdentifier = forIdentifier;
    
                                                    if (lastNode) {
                                                        parentNode.insertBefore(replacementNode, (processedName.includes('reverse') || processedName.includes('reversed') ? lastNode : lastNode.nextSibling));
                                                        lastNode = replacementNode;
                                                    } else {
                                                        replacementNode.forMasterNode = true;
                                                        lastNode = replacementNode;
                                                        if (masterNode) {
                                                            parentNode.replaceChild(replacementNode, masterNode);
                                                            masterNode = null;
                                                        } else {
                                                            parentNode.appendChild(replacementNode);
                                                        }
                                                    }
                                                }
                                            }
                                        } else {
                                            console.error("Targeted data-item is not a valid object.");
                                        }
                                    }
                                }
                            }));

                            node.removeAttribute(attribute.name);
                            break;
                        case 'value':
                            var target = attribute.value;
                            if (typeof node.value == 'string') {
                                const data = await new Promise(resolve => activeEventTransporter.dispatchEvent(new CustomEvent('retrieveData', { detail: { resolve } })));
                                node.value = getProperty(data, target);
                                activeEventTransporter.dispatchEvent(new CustomEvent('registerSchedulerValidator', {
                                    detail: {
                                        type: 'dom-synced',
                                        validator: () => node.value == getProperty(data, target)
                                    }
                                }));

                                activeEventTransporter.dispatchEvent(new CustomEvent('registerListener', {
                                    detail: {
                                        type: 'data:updated',
                                        listener: async item => {
                                            if (!item || item == target) {
                                                const denyList = ['object', 'function']
                                                if (denyList.includes(typeof node.value) || denyList.includes(typeof getProperty(data, target))) {
                                                    console.error('Cannot sync objects or functions.');
                                                } else if (node.value !== getProperty(data, target)) {
                                                    node.value = getProperty(data, target);
                                                    activeEventTransporter.dispatchEvent(new CustomEvent('triggerScheduledTasks'));
                                                }
                                            }
                                        }
                                    }
                                }));

                                node.addEventListener('input', e => {
                                    const denyList = ['object', 'function']
                                    if (denyList.includes(typeof node.value) || denyList.includes(typeof getProperty(data, target))) {
                                        console.error('Cannot sync objects or functions.');
                                    } else if (node.value !== getProperty(data, target)) {
                                        setProperty(data, target, node.value);
                                        activeEventTransporter.dispatchEvent(new CustomEvent('triggerScheduledTasks'));
                                    }
                                });
                            } else if (node.isContentEditable) {
                                const data = await new Promise(resolve => activeEventTransporter.dispatchEvent(new CustomEvent('retrieveData', { detail: { resolve } })));
                                node.innerText = getProperty(data, target);
                                activeEventTransporter.dispatchEvent(new CustomEvent('registerSchedulerValidator', {
                                    detail: {
                                        type: 'dom-synced',
                                        validator: () => node.innerText == getProperty(data, target)
                                    }
                                }));

                                activeEventTransporter.dispatchEvent(new CustomEvent('registerListener', {
                                    detail: {
                                        type: 'data:updated',
                                        listener: async item => {
                                            if (!item || item == target) {
                                                const denyList = ['object', 'function']
                                                if (denyList.includes(typeof node.innerText) || denyList.includes(typeof getProperty(data, target))) {
                                                    console.error('Cannot sync objects or functions.');
                                                } else if (node.innerText !== getProperty(data, target)) {
                                                    node.innerText = getProperty(data, target);
                                                    activeEventTransporter.dispatchEvent(new CustomEvent('triggerScheduledTasks'));
                                                }
                                            }
                                        }
                                    }
                                }));

                                node.addEventListener('input', e => {
                                    const denyList = ['object', 'function']
                                    if (denyList.includes(typeof node.innerText) || denyList.includes(typeof getProperty(data, target))) {
                                        console.error('Cannot sync objects or functions.');
                                    } else if (node.innerText !== getProperty(data, target)) {
                                        setProperty(data, target, node.innerText);
                                        activeEventTransporter.dispatchEvent(new CustomEvent('triggerScheduledTasks'));
                                    }
                                });
                            }

                            node.removeAttribute(attribute.name);                            
                            break;
                        case 'checked':
                            var target = attribute.value;
                            if (typeof node.checked == 'boolean') {
                                const data = await new Promise(resolve => activeEventTransporter.dispatchEvent(new CustomEvent('retrieveData', { detail: { resolve } })));
                                node.checked = getProperty(data, target);
                                activeEventTransporter.dispatchEvent(new CustomEvent('registerListener', {
                                    detail: {
                                        type: 'data:updated',
                                        listener: async item => {
                                            if (!item || item == target) {
                                                if (node.checked !== getProperty(data, target)) node.checked = getProperty(data, target);
                                            }
                                        }
                                    }
                                }));

                                node.addEventListener('input', e => {
                                    if (getProperty(data, target) !== node.checked) setProperty(data, target, node.checked);
                                });
                            }

                            node.removeAttribute(attribute.name); 
                            break;
                        case 'if':
                            var target = attribute.value;
                            if (logic_if[latestif]) latestif++;
                            logic_if[latestif] = [];
                            logic_if[latestif].push({
                                node: node,
                                validator: target
                            });

                            node.removeAttribute(attribute.name);
                            break;
                        case 'elseif':
                        case 'else-if':
                            var target = attribute.value;
                            if (!logic_if[latestif]) {
                                console.error("If statement should start with if block!");
                            } else {
                                logic_if[latestif].push({
                                    node: node,
                                    validator: target
                                });
                            }

                            node.removeAttribute(attribute.name);
                            break;
                        case 'else':
                            if (!logic_if[latestif]) {
                                console.error("If statement should start with if block!");
                            } else {
                                logic_if[latestif].push({
                                    node: node
                                });

                                latestif++;
                            }

                            node.removeAttribute(attribute.name);
                            break;
                        case 'class':
                            var target = attribute.value;
                            if (!processedName[1]) {
                                console.error("No class defined!");
                            } else {
                                const className = processedName[1];
                                activeEventTransporter.dispatchEvent(new CustomEvent('registerListener', {
                                    detail: {
                                        type: 'data:updated',
                                        listener: async () => {
                                            const data = await new Promise(resolve => activeEventTransporter.dispatchEvent(new CustomEvent('retrieveData', { detail: { resolve } })));
                                            const scopeData = await new Promise(resolve => activeEventTransporter.dispatchEvent(new CustomEvent('retrieveScopeData', { detail: { resolve } })));
                                            let keys = Object.keys(data);
                                            keys.push('return ' + target);
                                            let runner = Function.apply({}, keys);
                                            try {
                                                let res = runner.apply(scopeData, Object.values(data));
                                                if (res) {
                                                    node.classList.add(className);
                                                } else {
                                                    node.classList.remove(className);
                                                }
                                            } catch(e) {
                                                console.error(e);
                                                node.style.display = 'none';
                                            }
                                        }
                                    }
                                }));
                            }

                            node.removeAttribute(attribute.name);
                            break;
                        case 'bind':
                            var target = attribute.value;
                            const attrName = processedName[1];
                            activeEventTransporter.dispatchEvent(new CustomEvent('registerListener', {
                                detail: {
                                    type: 'data:updated',
                                    listener: async () => {
                                        const data = await new Promise(resolve => activeEventTransporter.dispatchEvent(new CustomEvent('retrieveData', { detail: { resolve } })));
                                        const scopeData = await new Promise(resolve => activeEventTransporter.dispatchEvent(new CustomEvent('retrieveScopeData', { detail: { resolve } })));
                                        let keys = Object.keys(data);
                                        keys.push('return ' + (target ? target : processedName[1]));
                                        let runner = Function.apply({}, keys);
                                        try {
                                            let res = runner.apply(scopeData, Object.values(data));
                                            node.setAttribute(attrName, res);
                                        } catch(e) {
                                            console.error(e);
                                        }
                                    }
                                }
                            }));

                            node.removeAttribute(attribute.name);
                            break;
                        case 'style':
                            var target = attribute.value;
                            var property = processedName[1];
                            property = property.split('_')[0] + property.split('_').slice(1).map(item => item.slice(0, 1).toUpperCase() + item.slice(1)).join('')

                            activeEventTransporter.dispatchEvent(new CustomEvent('registerListener', {
                                detail: {
                                    type: 'data:updated',
                                    listener: async () => {
                                        const data = await new Promise(resolve => activeEventTransporter.dispatchEvent(new CustomEvent('retrieveData', { detail: { resolve } })));
                                        const scopeData = await new Promise(resolve => activeEventTransporter.dispatchEvent(new CustomEvent('retrieveScopeData', { detail: { resolve } })));
                                        let keys = Object.keys(data);
                                        keys.push('return ' + target);
                                        let runner = Function.apply({}, keys);
                                        try {
                                            let res = runner.apply(scopeData, Object.values(data));
                                            node.style[property] = res;
                                        } catch(e) {
                                            console.error(e);
                                        }
                                    }
                                }
                            }));

                            node.removeAttribute(attribute.name);
                            break;
                    }
                }
            });

            if (node.childNodes.length > 0 && !node.doNotProcessChildNodes) processElementAttributes(node, activeEventTransporter, components, (component ? component.data : rawData));
            if (component) {
                processTextNodes(node, activeEventTransporter);
                node.doNotProcessTextNodes = true;
                component.eventTransporter.dispatchEvent(new CustomEvent('triggerListeners', { detail: { listeners: 'data:updated', additionalInformation: false } } ))
            }
        }
    });
}

export { Rable };
